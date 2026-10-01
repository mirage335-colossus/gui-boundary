#include "clipboard.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

using gui::rev::Clipboard;
using gui::rev::ClipboardResult;
using Clock = std::chrono::steady_clock;

namespace {
void wait_for(const std::function<bool()>& done, const std::function<void()>& pump) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!done() && Clock::now() < deadline) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(done());
}
}

int main() {
    Clipboard owner, reader;
    const auto pump = [&] { owner.poll(); reader.poll(); };
    const auto roundtrip = [&](const std::string& text) {
        owner.copy(text);
        std::optional<ClipboardResult> output;
        reader.paste([&](auto result) { output = std::move(result); });
        assert(reader.pending() && !output);
        wait_for([&] { return output.has_value(); }, pump);
        assert(!reader.pending());
        assert(output->error.empty());
        assert(output->text == text);
    };
    roundtrip("Unicode café 雪 😀");
    roundtrip("");
    // This exceeds the X11 request size and therefore exercises INCR as well as
    // bounded native conversion/allocation on Windows.
    roundtrip(std::string(3 * 1024 * 1024, 'x') + " 雪");

    const auto rejected_copy = [&](const std::string& text) {
        bool rejected = false;
        try { owner.copy(text); } catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    };
    rejected_copy(std::string("zero\0byte", 9));
    rejected_copy(std::string("\xc0\xaf", 2));
    rejected_copy(std::string(16 * 1024 * 1024 + 1, 'x'));

    owner.copy("cancelled");
    bool stale = false;
    reader.paste([&](auto) { stale = true; });
    bool duplicate_rejected = false;
    try { reader.paste([](auto) {}); } catch (const std::runtime_error&) { duplicate_rejected = true; }
    assert(duplicate_rejected);
    reader.cancel();
    assert(!reader.pending());
    owner.copy("new request");
    std::optional<ClipboardResult> fresh;
    reader.paste([&](auto result) { fresh = std::move(result); });
    wait_for([&] { return fresh.has_value(); }, pump);
    assert(!stale);
    assert(fresh->text == "new request");

    // A queued ownership-loss event must not erase a later copy by that owner.
    owner.copy("first");
    reader.copy("second");
    owner.copy("third");
    std::optional<ClipboardResult> latest;
    reader.paste([&](auto result) { latest = std::move(result); });
    wait_for([&] { return latest.has_value(); }, pump);
    assert(latest->text == "third");

#ifndef _WIN32
    // A separate, untrusted selection owner bypasses our copy validation.
    // Receiving an embedded zero byte must report failure, never an empty paste.
    auto* display = XOpenDisplay(nullptr);
    assert(display);
    const auto window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    const auto selection = XInternAtom(display, "CLIPBOARD", False);
    const auto utf8 = XInternAtom(display, "UTF8_STRING", False);
    XSetSelectionOwner(display, selection, window, CurrentTime);
    XFlush(display);
    std::optional<ClipboardResult> invalid;
    reader.paste([&](auto result) { invalid = std::move(result); });
    wait_for([&] { return invalid.has_value(); }, [&] {
        const int count = XPending(display);
        for (int i = 0; i < count; ++i) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != SelectionRequest) continue;
            const auto& request = event.xselectionrequest;
            const auto property = request.property == None ? request.target : request.property;
            const unsigned char value[]{'a', 0, 'b'};
            XChangeProperty(display, request.requestor, property, utf8, 8, PropModeReplace, value, 3);
            XEvent reply{};
            auto& response = reply.xselection;
            response.type = SelectionNotify;
            response.display = display;
            response.requestor = request.requestor;
            response.selection = request.selection;
            response.target = request.target;
            response.time = request.time;
            response.property = property;
            XSendEvent(display, request.requestor, False, 0, &reply);
            XFlush(display);
        }
        pump();
    });
    assert(!invalid->text && !invalid->error.empty());
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    std::optional<ClipboardResult> absent;
    reader.paste([&](auto result) { absent = std::move(result); });
    wait_for([&] { return absent.has_value(); }, pump);
    assert(!absent->text && !absent->error.empty());
#endif
    std::cout << "Native clipboard text, transfer bounds, cancellation and ownership checks passed.\n";
}
