#include "clipboard.hpp"
#include "gui/text.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

namespace gui::rev {
namespace {
constexpr std::size_t text_limit = 16 * 1024 * 1024;
void validate_text(const std::string& text) {
    if (text.size() > text_limit) throw std::runtime_error("Clipboard text exceeds 16 MiB");
    if (!valid_utf8(text)) throw std::runtime_error("Clipboard text must be UTF-8 without zero bytes");
}
}

#ifdef _WIN32
struct Clipboard::Impl {
    HWND window = nullptr;
    std::function<void(ClipboardResult)> receive;
    ClipboardResult ready;
};

Clipboard::Clipboard() : impl_(std::make_unique<Impl>()) {
    impl_->window = CreateWindowExW(0, L"STATIC", L"GUI boundary clipboard", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!impl_->window) throw std::runtime_error("Cannot create the clipboard service window");
}
Clipboard::~Clipboard() { cancel(); DestroyWindow(impl_->window); }
bool Clipboard::pending() const noexcept { return bool(impl_->receive); }
void Clipboard::cancel() noexcept { impl_->receive = {}; impl_->ready = {}; }
void Clipboard::poll() {
    auto result = std::move(impl_->ready);
    if (auto completion = std::exchange(impl_->receive, {})) completion(std::move(result));
}

void Clipboard::copy(const std::string& text) {
    validate_text(text); // The bound also makes all Win32 int conversions safe.
    const int bytes = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, nullptr, 0);
    if (!count && !text.empty()) throw std::runtime_error("Cannot convert clipboard text");
    std::wstring value(static_cast<std::size_t>(count), L'\0');
    if (count && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, value.data(), count) != count)
        throw std::runtime_error("Cannot convert clipboard text");
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    if (!data) throw std::runtime_error("Cannot allocate clipboard text");
    auto* memory = GlobalLock(data);
    if (!memory) { GlobalFree(data); throw std::runtime_error("Cannot lock clipboard text"); }
    std::memcpy(memory, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(data);
    if (!OpenClipboard(impl_->window)) { GlobalFree(data); throw std::runtime_error("Clipboard is busy"); }
    struct Close { ~Close() { CloseClipboard(); } } close;
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, data)) {
        GlobalFree(data);
        throw std::runtime_error("Cannot copy text to the clipboard");
    }
    // SetClipboardData transfers allocation ownership to the operating system.
}

void Clipboard::paste(std::function<void(ClipboardResult)> completion) {
    if (!completion) throw std::invalid_argument("Clipboard completion is required");
    if (pending()) throw std::runtime_error("A clipboard read is already pending");
    ClipboardResult result{std::nullopt, "Clipboard has no text"};
    if (!OpenClipboard(impl_->window)) result.error = "Clipboard is busy";
    else {
        struct Close { ~Close() { CloseClipboard(); } } close;
        if (auto data = GetClipboardData(CF_UNICODETEXT)) {
            const auto allocation = GlobalSize(data);
            if (allocation < sizeof(wchar_t) || allocation % sizeof(wchar_t) != 0 ||
                allocation > (text_limit + 1) * sizeof(wchar_t)) {
                result.error = "Clipboard text allocation is invalid or too large";
            } else if (auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                struct Unlock { HGLOBAL value; ~Unlock() { GlobalUnlock(value); } } unlock{data};
                const auto units = allocation / sizeof(wchar_t);
                const auto end = std::find(text, text + units, L'\0');
                if (end == text + units) result.error = "Clipboard text is not terminated";
                else if (end == text) result = {std::string{}, {}};
                else {
                    const int length = static_cast<int>(end - text);
                    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr);
                    if (bytes <= 0 || static_cast<std::size_t>(bytes) > text_limit)
                        result.error = "Clipboard text is invalid Unicode or exceeds 16 MiB";
                    else {
                        std::string value(static_cast<std::size_t>(bytes), '\0');
                        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, value.data(), bytes, nullptr, nullptr) != bytes)
                            result.error = "Cannot convert clipboard text";
                        else result = {std::move(value), {}};
                    }
                }
            } else result.error = "Cannot lock clipboard text";
        }
    }
    impl_->ready = std::move(result);
    impl_->receive = std::move(completion);
}
#else
struct Clipboard::Impl {
    inline static std::vector<Display*> displays;
    inline static XErrorHandler previous_handler = nullptr;
    static int xerror(Display* display, XErrorEvent* error) {
        // Requestors can disappear during INCR. Suppress only that expected race
        // on our own connection; unrelated native errors retain their handler.
        if (error->error_code == BadWindow && std::find(displays.begin(), displays.end(), display) != displays.end()) return 0;
        return previous_handler ? previous_handler(display, error) : 0;
    }
    Display* display = nullptr;
    Window window = 0, requestor = 0;
    Atom clipboard = 0, utf8 = 0, targets = 0, property = 0, incr = 0;
    std::shared_ptr<const std::string> text = std::make_shared<const std::string>();
    std::string incoming;
    bool incremental = false;
    std::function<void(ClipboardResult)> receive;
    std::optional<ClipboardResult> ready;
    using Clock = std::chrono::steady_clock;
    Clock::time_point started, touched;
    struct Send {
        Window window;
        Atom property;
        std::shared_ptr<const std::string> text;
        std::size_t offset;
        Clock::time_point started, touched;
    };
    std::vector<Send> sends;
    std::size_t chunk = 32768;
    static constexpr auto idle_timeout = std::chrono::seconds(3);
    static constexpr auto total_timeout = std::chrono::seconds(15);
    void stop_read() noexcept {
        incremental = false;
        incoming.clear();
        if (requestor) { XDestroyWindow(display, requestor); requestor = 0; }
    }
    void finish(ClipboardResult result) { stop_read(); ready = std::move(result); }
};

Clipboard::Clipboard() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.display = XOpenDisplay(nullptr);
    if (!p.display) throw std::runtime_error("Cannot connect to X11 for clipboard services");
    if (Impl::displays.empty()) Impl::previous_handler = XSetErrorHandler(Impl::xerror);
    Impl::displays.push_back(p.display);
    p.window = XCreateSimpleWindow(p.display, DefaultRootWindow(p.display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(p.display, p.window, PropertyChangeMask);
    p.clipboard = XInternAtom(p.display, "CLIPBOARD", False);
    p.utf8 = XInternAtom(p.display, "UTF8_STRING", False);
    p.targets = XInternAtom(p.display, "TARGETS", False);
    p.property = XInternAtom(p.display, "GUI_BOUNDARY_CLIPBOARD", False);
    p.incr = XInternAtom(p.display, "INCR", False);
    const auto maximum = XMaxRequestSize(p.display);
    if (maximum > 64) p.chunk = std::min(p.chunk, static_cast<std::size_t>(maximum - 64) * 4);
}
Clipboard::~Clipboard() {
    cancel();
    XDestroyWindow(impl_->display, impl_->window);
    XCloseDisplay(impl_->display);
    std::erase(Impl::displays, impl_->display);
    if (Impl::displays.empty()) XSetErrorHandler(Impl::previous_handler);
}
bool Clipboard::pending() const noexcept { return bool(impl_->receive); }
void Clipboard::cancel() noexcept {
    impl_->receive = {};
    impl_->ready.reset();
    impl_->stop_read();
}
void Clipboard::copy(const std::string& text) {
    validate_text(text);
    auto& p = *impl_;
    p.text = std::make_shared<const std::string>(text);
    XSetSelectionOwner(p.display, p.clipboard, p.window, CurrentTime);
    XFlush(p.display);
    if (XGetSelectionOwner(p.display, p.clipboard) != p.window)
        throw std::runtime_error("Cannot own the X11 clipboard");
}
void Clipboard::paste(std::function<void(ClipboardResult)> completion) {
    if (!completion) throw std::invalid_argument("Clipboard completion is required");
    if (pending()) throw std::runtime_error("A clipboard read is already pending");
    auto& p = *impl_;
    p.receive = std::move(completion);
    p.ready.reset();
    p.incoming.clear();
    p.incremental = false;
    p.started = p.touched = Impl::Clock::now();
    // A fresh window is an operation identity: a late reply after cancel/timeout
    // cannot be mistaken for the next paste into another control.
    p.requestor = XCreateSimpleWindow(p.display, DefaultRootWindow(p.display), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(p.display, p.requestor, PropertyChangeMask);
    XConvertSelection(p.display, p.clipboard, p.utf8, p.property, p.requestor, CurrentTime);
    XFlush(p.display);
}

void Clipboard::poll() {
    auto& p = *impl_;
    const auto now = Impl::Clock::now();
    std::erase_if(p.sends, [&](const auto& send) {
        return now - send.touched > Impl::idle_timeout || now - send.started > Impl::total_timeout;
    });
    const int count = std::min(64, XPending(p.display));
    for (int i = 0; i < count; ++i) {
        XEvent event;
        XNextEvent(p.display, &event);
        if (event.type == SelectionRequest) {
            const auto& request = event.xselectionrequest;
            if (request.selection != p.clipboard || request.owner != p.window) continue;
            XEvent reply{};
            auto& selection = reply.xselection;
            selection.type = SelectionNotify;
            selection.display = request.display;
            selection.requestor = request.requestor;
            selection.selection = request.selection;
            selection.target = request.target;
            selection.time = request.time;
            selection.property = None;
            const Atom property = request.property == None ? request.target : request.property;
            if (request.target == p.targets) {
                Atom formats[]{p.targets, p.utf8};
                XChangeProperty(p.display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(formats), 2);
                selection.property = property;
            } else if (request.target == p.utf8) {
                if (p.text->size() <= p.chunk) {
                    XChangeProperty(p.display, request.requestor, property, p.utf8, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(p.text->data()), static_cast<int>(p.text->size()));
                    selection.property = property;
                } else {
                    std::size_t retained = p.text->size();
                    for (const auto& send : p.sends) retained += send.text->size();
                    const bool duplicate = std::any_of(p.sends.begin(), p.sends.end(), [&](const auto& send) {
                        return send.window == request.requestor && send.property == property;
                    });
                    if (!duplicate && p.sends.size() < 4 && retained <= 4 * text_limit) {
                        unsigned long size = static_cast<unsigned long>(p.text->size());
                        XSelectInput(p.display, request.requestor, PropertyChangeMask | StructureNotifyMask);
                        XChangeProperty(p.display, request.requestor, property, p.incr, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&size), 1);
                        p.sends.push_back({request.requestor, property, p.text, 0, now, now});
                        selection.property = property;
                    }
                }
            }
            XSendEvent(p.display, request.requestor, False, 0, &reply);
        } else if (event.type == SelectionClear && event.xselectionclear.selection == p.clipboard) {
            // Ownership may already have returned after this queued event.
            if (XGetSelectionOwner(p.display, p.clipboard) != p.window)
                p.text = std::make_shared<const std::string>();
        } else if (event.type == PropertyNotify && event.xproperty.state == PropertyDelete) {
            for (auto it = p.sends.begin(); it != p.sends.end();) {
                if (it->window != event.xproperty.window || it->property != event.xproperty.atom) { ++it; continue; }
                const auto length = std::min(p.chunk, it->text->size() - it->offset);
                XChangeProperty(p.display, it->window, it->property, p.utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(it->text->data() + it->offset), static_cast<int>(length));
                it->offset += length;
                it->touched = now;
                if (!length) it = p.sends.erase(it); else ++it;
            }
        } else if (event.type == DestroyNotify) {
            std::erase_if(p.sends, [&](const auto& send) { return send.window == event.xdestroywindow.window; });
        } else if (p.receive && !p.ready &&
            ((event.type == SelectionNotify && event.xselection.requestor == p.requestor &&
              event.xselection.selection == p.clipboard && event.xselection.target == p.utf8 &&
              (event.xselection.property == p.property || event.xselection.property == None)) ||
             (event.type == PropertyNotify && p.incremental && event.xproperty.window == p.requestor &&
              event.xproperty.atom == p.property && event.xproperty.state == PropertyNewValue))) {
            if (event.type == SelectionNotify && event.xselection.property == None) {
                p.finish({std::nullopt, "Clipboard has no UTF-8 text"});
                continue;
            }
            Atom type = 0;
            int format = 0;
            unsigned long length = 0, remaining = 0;
            unsigned char* data = nullptr;
            const int status = XGetWindowProperty(p.display, p.requestor, p.property, 0,
                static_cast<long>(text_limit / 4), True, AnyPropertyType,
                &type, &format, &length, &remaining, &data);
            if (status == Success && type == p.incr && !p.incremental && format == 32 &&
                length == 1 && !remaining && data && *reinterpret_cast<unsigned long*>(data) <= text_limit) {
                p.incremental = true;
                XDeleteProperty(p.display, p.requestor, p.property);
                p.touched = now;
            } else if (status == Success && type == p.utf8 && format == 8 && !remaining &&
                       length <= text_limit - p.incoming.size() && (!length || data)) {
                if (length) p.incoming.append(reinterpret_cast<char*>(data), static_cast<std::size_t>(length));
                p.touched = now;
                if (!p.incremental || !length) {
                    if (valid_utf8(p.incoming)) { auto text = std::move(p.incoming); p.finish({std::move(text), {}}); }
                    else p.finish({std::nullopt, "Clipboard text must be UTF-8 without zero bytes"});
                }
            } else p.finish({std::nullopt, "Clipboard text is invalid or exceeds 16 MiB"});
            if (data) XFree(data);
        }
    }
    if (p.receive && !p.ready &&
        (Impl::Clock::now() - p.touched > Impl::idle_timeout || Impl::Clock::now() - p.started > Impl::total_timeout))
        p.finish({std::nullopt, "Clipboard read timed out"});
    XFlush(p.display);
    if (p.ready) {
        auto result = std::move(*p.ready);
        p.ready.reset();
        // Deliver last, allowing the callback to start another read or destroy
        // its owner without leaving poll() with references into that owner.
        if (auto completion = std::exchange(p.receive, {})) completion(std::move(result));
    }
}
#endif
}
