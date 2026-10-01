module;

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <stdexcept>
#include <functional>
#include <string>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <poll.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xresource.h>
#if __has_include(<X11/extensions/Xrandr.h>)
#define REV_HAS_XRANDR 1
#include <X11/extensions/Xrandr.h>
#endif
#if __has_include(<X11/extensions/sync.h>)
#define REV_HAS_XSYNC 1
#include <X11/extensions/sync.h>
#endif
#include <glew/glew.h>
#include <GL/glx.h>
#include <dbg.hpp>

#include "WinEvent.hpp"

export module Rev.NativeWindow;

import Rev.Appearance;

export namespace Rev {

    struct NativeWindow {

        enum ButtonAction { Release, Press, DoubleClick };
        enum MouseButton { Left, Right, Middle };

        enum class Key : int {
            Unknown,
            Ctrl, Shift, Alt, Super,
            Up, Down, Left, Right,
            PageUp, PageDown, Home, End, Insert, Delete,
            F1, F2, F3, F4, F5, F6,
            F7, F8, F9, F10, F11, F12,
            Num0, Num1, Num2, Num3, Num4,
            Num5, Num6, Num7, Num8, Num9,
            A, B, C, D, E, F, G, H, I, J,
            K, L, M, N, O, P, Q, R, S, T,
            U, V, W, X, Y, Z,
            Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
            Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
            NumpadAdd, NumpadSub, NumpadMul, NumpadDiv, NumpadEnter, NumpadDecimal,
            Escape, Space, Tab, Enter, Backspace,
            CapsLock, NumLock, ScrollLock,
            PrintScreen, Pause,
        };

        struct Display {
            int handle = 0;
            std::string friendlyName;
            int x, y, w, h;
            bool primary = false;
        };

        static std::vector<Display> getDisplays() {
            ensureDisplay();
            std::vector<Display> displays;

#if REV_HAS_XRANDR
            int eventBase = 0;
            int errorBase = 0;
            if (XRRQueryExtension(xDisplay, &eventBase, &errorBase)) {
                ::Window root = RootWindow(xDisplay, screen);
                XRRScreenResources* resources = XRRGetScreenResourcesCurrent(xDisplay, root);
                if (resources) {
                    RROutput primary = XRRGetOutputPrimary(xDisplay, root);
                    for (int i = 0; i < resources->noutput; ++i) {
                        XRROutputInfo* output = XRRGetOutputInfo(xDisplay, resources, resources->outputs[i]);
                        if (!output) continue;

                        if (output->connection == RR_Connected && output->crtc) {
                            XRRCrtcInfo* crtc = XRRGetCrtcInfo(xDisplay, resources, output->crtc);
                            if (crtc) {
                                displays.push_back({
                                    .handle = static_cast<int>(resources->outputs[i]),
                                    .friendlyName = output->name ? std::string(output->name, output->nameLen) : "Display",
                                    .x = crtc->x,
                                    .y = crtc->y,
                                    .w = static_cast<int>(crtc->width),
                                    .h = static_cast<int>(crtc->height),
                                    .primary = primary == resources->outputs[i]
                                });
                                XRRFreeCrtcInfo(crtc);
                            }
                        }

                        XRRFreeOutputInfo(output);
                    }
                    XRRFreeScreenResources(resources);
                }
            }
#endif

            if (displays.empty()) {
                displays.push_back({
                    .handle = screen,
                    .friendlyName = "Default Display",
                    .x = 0,
                    .y = 0,
                    .w = DisplayWidth(xDisplay, screen),
                    .h = DisplayHeight(xDisplay, screen),
                    .primary = true
                });
            }

            bool hasPrimary = false;
            for (const auto& display : displays) {
                if (display.primary) { hasPrimary = true; break; }
            }
            if (!hasPrimary && !displays.empty()) displays[0].primary = true;

            return displays;
        }

        struct Size { int w, h, minW, minH, maxW, maxH; };

        struct Details {
            std::string name = "Hello World";
            Size size = { 640, 480, 0, 0, 1000, 1000 };
            bool decorated = true;
            bool resizable = true;
            bool borderless = false;
            bool fullscreen = false;
            bool closeButton = true;
            bool minimizeButton = true;
            bool maximizeButton = true;
            bool nativeFrameless = false;
        };

        enum class Relationship {
            Independent,
            OwnedTopLevel,
            EmbeddedChild
        };

        using EventCallback = std::function<void(WinEvent&)>;

        inline static ::Display* xDisplay = nullptr;
        inline static int screen = 0;
        inline static Atom wmDeleteWindow = 0;
        inline static std::unordered_map<::Window, NativeWindow*> windows;
        inline static GLXContext sharedRoot = nullptr;
        inline static size_t liveWindows = 0;
        inline static bool glewLoaded = false;
        inline static XIM inputMethod = nullptr;
        inline static Atom wmState = 0;
        inline static Atom wmStateFullscreen = 0;
        inline static Atom wmStateMaximizedVert = 0;
        inline static Atom wmStateMaximizedHorz = 0;
        inline static Atom wmStateHidden = 0;
        inline static Atom motifWmHints = 0;
        inline static Atom wmProtocols = 0;
        inline static Atom wmSyncRequest = 0;
        inline static Atom wmSyncRequestCounter = 0;
        inline static Atom revFrameRequest = 0;
#if REV_HAS_XSYNC
        inline static bool xsyncAvailable = false;
        inline static int xsyncEventBase = 0;
        inline static int xsyncErrorBase = 0;
#endif

        void* handle = nullptr;
        ::Window xWindow = 0;
        GLXContext glContext = nullptr;
        Colormap colormap = 0;
        XVisualInfo* visual = nullptr;
        bool ownsVisual = true;
        EventCallback callback;
        XIC inputContext = nullptr;
        ::Cursor xCursor = 0;
#if REV_HAS_XSYNC
        XSyncCounter syncCounter = None;
        XSyncValue syncValue{};
        XSyncValue pendingSyncValue{};
        bool hasPendingSync = false;
#endif

        Size size;
        float scale = 1.0f;
        int posX = 0;
        int posY = 0;
        Appearance::Cursor cursor;
        bool dirty = false;
        bool frameQueued = false;
        bool closed = false;
        bool minimized = false;
        bool maximized = false;
        bool fullscreen = false;
        Relationship relationship = Relationship::EmbeddedChild;

        NativeWindow(::Window parent, Size size = { 640, 480, 0, 0, 1000, 1000 }, EventCallback callback = nullptr)
            : NativeWindow(reinterpret_cast<void*>(static_cast<uintptr_t>(parent)), Details{ .size = size }, callback) {}

        NativeWindow(void* parent, Size size = { 640, 480, 0, 0, 1000, 1000 }, EventCallback callback = nullptr)
            : NativeWindow(parent, Details{ .size = size }, callback) {}

        NativeWindow(
            void* parent,
            Details details,
            EventCallback callback = nullptr,
            Relationship relationship = Relationship::EmbeddedChild
        ) {
            this->size = details.size;
            this->callback = callback;
            this->relationship = relationship;

            ensureDisplay();
            scale = displayScale();

            visual = chooseVisual();
            if (!visual) throw std::runtime_error("[NativeWindow] failed to choose GLX visual");
            ::Window root = RootWindow(xDisplay, screen);
            colormap = XCreateColormap(xDisplay, root, visual->visual, AllocNone);

            XSetWindowAttributes swa{};
            swa.colormap = colormap;
            swa.event_mask = ExposureMask | StructureNotifyMask | FocusChangeMask |
                             PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                             KeyPressMask | KeyReleaseMask | PropertyChangeMask;

            ::Window parentWindow = parent ? static_cast<::Window>(reinterpret_cast<uintptr_t>(parent)) : root;

            auto [initialX, initialY] = initialWindowPosition(details.size);
            if (relationship == Relationship::EmbeddedChild || parentWindow != root) {
                initialX = 0;
                initialY = 0;
            }
            posX = initialX;
            posY = initialY;

            xWindow = XCreateWindow(
                xDisplay,
                parentWindow,
                initialX, initialY,
                static_cast<unsigned int>(size.w), static_cast<unsigned int>(size.h),
                0,
                visual->depth,
                InputOutput,
                visual->visual,
                CWColormap | CWEventMask,
                &swa
            );

            if (!xWindow) throw std::runtime_error("[NativeWindow] XCreateWindow failed");

            windows[xWindow] = this;
            setTitle(details.name);
            createSyncCounter();
            Atom protocols[] = { wmDeleteWindow };
            XSetWMProtocols(xDisplay, xWindow, protocols, 1);
            applyWindowManagerHints(details);
            createInputContext();

            glContext = glXCreateContext(xDisplay, visual, sharedRoot, GL_TRUE);
            if (!glContext) throw std::runtime_error("[NativeWindow] glXCreateContext failed");
            if (!sharedRoot) sharedRoot = glContext;
            liveWindows++;

            XMapWindow(xDisplay, xWindow);
            XFlush(xDisplay);

            handle = reinterpret_cast<void*>(static_cast<uintptr_t>(xWindow));

            notifyEvent({ WinEvent::Type::Create });
            notifyEvent({ WinEvent::Type::Scale, 0, 0, static_cast<int>(scale * 100.0f), static_cast<int>(scale * 100.0f) });
            notifyEvent({ WinEvent::Type::Resize, 0, 0, size.w, size.h });
        }

        ~NativeWindow() {
            if (xDisplay && xWindow) {
                windows.erase(xWindow);

                // Drop any pending X events for this window before releasing
                // the C++ object. Otherwise queued Configure/Expose/Destroy
                // events can be delivered later with stale NativeWindow* data.
                XEvent pending{};
                while (XCheckWindowEvent(
                    xDisplay,
                    xWindow,
                    ExposureMask | StructureNotifyMask | FocusChangeMask |
                    PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
                    KeyPressMask | KeyReleaseMask,
                    &pending
                )) {}

                if (glContext) {
                    if (glXGetCurrentContext() == glContext) glXMakeCurrent(xDisplay, None, nullptr);
                    if (glContext != sharedRoot) {
                        glXDestroyContext(xDisplay, glContext);
                    }
                    glContext = nullptr;
                }

                if (liveWindows > 0) { liveWindows--; }

                if (liveWindows == 0 && sharedRoot) {
                    glXDestroyContext(xDisplay, sharedRoot);
                    sharedRoot = nullptr;
                    glewLoaded = false;
                }

                destroySyncCounter();

                if (inputContext) {
                    XDestroyIC(inputContext);
                    inputContext = nullptr;
                }

                if (xCursor) {
                    XFreeCursor(xDisplay, xCursor);
                    xCursor = 0;
                }

                XDestroyWindow(xDisplay, xWindow);
                XFlush(xDisplay);
                xWindow = 0; handle = nullptr;
            }
            if (visual) XFree(visual);
            if (xDisplay && colormap) XFreeColormap(xDisplay, colormap);
        }

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) {
            switch (key) {
                case Key::Ctrl: return "Ctrl"; case Key::Shift: return "Shift";
                case Key::Alt: return "Alt"; case Key::Super: return "Super";
                case Key::Up: return "Up"; case Key::Down: return "Down";
                case Key::Left: return "Left"; case Key::Right: return "Right";
                case Key::Escape: return "Escape"; case Key::Space: return "Space";
                case Key::Tab: return "Tab"; case Key::Enter: return "Enter";
                case Key::Backspace: return "Backspace"; case Key::Delete: return "Delete";
                default: return "Unknown";
            }
        }

        void show() {
            XMapRaised(xDisplay, xWindow);
            XFlush(xDisplay);
        }

        void setPos(int x, int y) {
            posX = x;
            posY = y;
            XMoveWindow(xDisplay, xWindow, x, y);
        }

        void getClientPos(int& x, int& y) const {
            ::Window child = 0;
            x = posX;
            y = posY;
            if (xDisplay && xWindow) {
                XTranslateCoordinates(xDisplay, xWindow, RootWindow(xDisplay, screen),
                                      0, 0, &x, &y, &child);
            }
        }

        void setIcon(const unsigned char* rgba, int w, int h, bool big) {
            if (!xDisplay || !xWindow || !rgba || w <= 0 || h <= 0) return;
            // EWMH accepts several sizes in one CARDINAL property. Window calls
            // big first, then small; each item is an ARGB pixel in native long storage.
            std::vector<unsigned long> pixels;
            pixels.reserve(static_cast<size_t>(w) * static_cast<size_t>(h) + 2);
            pixels.push_back(static_cast<unsigned long>(w));
            pixels.push_back(static_cast<unsigned long>(h));
            for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i) {
                const unsigned char* p = rgba + i * 4;
                pixels.push_back((static_cast<unsigned long>(p[3]) << 24) |
                                 (static_cast<unsigned long>(p[0]) << 16) |
                                 (static_cast<unsigned long>(p[1]) << 8) | p[2]);
            }
            const Atom property = XInternAtom(xDisplay, "_NET_WM_ICON", False);
            XChangeProperty(xDisplay, xWindow, property, XA_CARDINAL, 32,
                            big ? PropModeReplace : PropModeAppend,
                            reinterpret_cast<const unsigned char*>(pixels.data()),
                            static_cast<int>(pixels.size()));
        }

        void setTitle(const std::string& title) {
            XStoreName(xDisplay, xWindow, title.c_str());
        }

        void setRect(int x, int y, int w, int h) {
            posX = x;
            posY = y;
            size.w = w;
            size.h = h;
            XMoveResizeWindow(xDisplay, xWindow, x, y, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
        }

        void setSize(int w, int h) {
            size.w = w;
            size.h = h;
            XResizeWindow(xDisplay, xWindow, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
        }

        void setCursor(Appearance::Cursor newCursor) {
            cursor = newCursor;
            if (!xDisplay || !xWindow) return;

            if (xCursor) {
                XFreeCursor(xDisplay, xCursor);
                xCursor = 0;
            }

            unsigned int shape = cursorShape(newCursor);
            if (shape != 0) {
                xCursor = XCreateFontCursor(xDisplay, shape);
                XDefineCursor(xDisplay, xWindow, xCursor);
            }
            else {
                XUndefineCursor(xDisplay, xWindow);
            }
            XFlush(xDisplay);
        }

        void requestFrame(bool force = false) {
            if (frameQueued && !force) return;
            dirty = true;
            frameQueued = true;

            XEvent ev{};
            ev.type = ClientMessage;
            ev.xclient.display = xDisplay;
            ev.xclient.window = xWindow;
            ev.xclient.message_type = revFrameRequest;
            ev.xclient.format = 32;
            XSendEvent(xDisplay, xWindow, False, NoEventMask, &ev);
            XFlush(xDisplay);
        }

        WinEvent notifyEvent(WinEvent event) {
            event.subject = this;

            // Keep desktop coordinates in signed physical pixels. Window alone
            // subtracts the client origin and applies the DPI scale.
            if (callback) callback(event);
            return event;
        }

        struct ResourceContextGuard {
            NativeWindow* previous = nullptr;

            ResourceContextGuard(NativeWindow* window) {
                previous = currentWindow();
                if (window) window->makeContextCurrent();
            }

            ~ResourceContextGuard() {
                if (previous) previous->makeContextCurrent();
            }
        };

        static NativeWindow* currentWindow() {
            GLXContext current = glXGetCurrentContext();
            if (!current) return nullptr;
            for (auto& [_, window] : windows) {
                if (window && window->glContext == current) return window;
            }
            return nullptr;
        }

        static NativeWindow* requireContext(void* context, const char* operation = "OpenGL operation") {
            if (!context) throw std::runtime_error(std::string("[NativeWindow] Missing context for ") + operation);
            NativeWindow* window = static_cast<NativeWindow*>(context);
            window->makeContextCurrent();
            return window;
        }

        void createContext() {
            makeContextCurrent();
        }

        bool isContextCurrent() const {
            return glXGetCurrentContext() == glContext;
        }

        bool tryMakeContextCurrent() {
            return glXMakeCurrent(xDisplay, xWindow, glContext);
        }

        void makeContextCurrent() {
            if (!tryMakeContextCurrent()) {
                throw std::runtime_error("[NativeWindow] glXMakeCurrent failed");
            }
        }

        void loadGlFunctions() {
            if (!isContextCurrent()) { makeContextCurrent(); }

            if (!glewLoaded) {
                glewExperimental = GL_TRUE;
                GLenum status = glewInit();
                glGetError();
                if (status != GLEW_OK) {
                    throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(status)));
                }
                glewLoaded = true;
            }

            validateGlCapabilities();

            dbg("OpenGL INFO");
            dbg("GLEW version: %s", glewGetString(GLEW_VERSION));
            dbg("OpenGL version: %s", glGetString(GL_VERSION));
        }

        void swapBuffers() {
            glXSwapBuffers(xDisplay, xWindow);
            completeSyncRequest();
            dirty = false;
        }

        static void validateGlCapabilities() {
            if (!GLEW_VERSION_4_3 || !(GLEW_VERSION_4_4 || GLEW_ARB_buffer_storage)) {
                throw std::runtime_error("[NativeWindow] Rev requires OpenGL 4.4, or OpenGL 4.3 with GL_ARB_buffer_storage (including software drivers)");
            }
        }

        static XVisualInfo* chooseVisual() {
            int preferred[] = {
                GLX_RGBA,
                GLX_DOUBLEBUFFER,
                GLX_DEPTH_SIZE, 24,
                GLX_STENCIL_SIZE, 8,
                GLX_RED_SIZE, 8,
                GLX_GREEN_SIZE, 8,
                GLX_BLUE_SIZE, 8,
                GLX_ALPHA_SIZE, 8,
                None
            };

            XVisualInfo* selected = glXChooseVisual(xDisplay, screen, preferred);
            if (selected) return selected;

            int fallback[] = {
                GLX_RGBA,
                GLX_DOUBLEBUFFER,
                GLX_DEPTH_SIZE, 24,
                GLX_STENCIL_SIZE, 8,
                GLX_RED_SIZE, 8,
                GLX_GREEN_SIZE, 8,
                GLX_BLUE_SIZE, 8,
                None
            };

            return glXChooseVisual(xDisplay, screen, fallback);
        }

        static float displayScale() {
            ensureDisplay();

            if (const char* forcedScale = std::getenv("REV_SCALE")) {
                float value = std::strtof(forcedScale, nullptr);
                if (value > 0.0f) return std::clamp(value, 0.5f, 4.0f);
            }

            if (const char* toolkitScale = std::getenv("GDK_SCALE")) {
                float value = std::strtof(toolkitScale, nullptr);
                if (value > 0.0f) return std::clamp(value, 0.5f, 4.0f);
            }

            if (const char* qtScale = std::getenv("QT_SCALE_FACTOR")) {
                float value = std::strtof(qtScale, nullptr);
                if (value > 0.0f) return std::clamp(value, 0.5f, 4.0f);
            }

            float xftDpi = xftDpiValue();
            if (xftDpi > 0.0f) return std::clamp(xftDpi / 96.0f, 0.5f, 4.0f);

            int mmWidth = DisplayWidthMM(xDisplay, screen);
            int pxWidth = DisplayWidth(xDisplay, screen);
            if (mmWidth <= 0 || pxWidth <= 0) return 1.0f;

            float dpi = static_cast<float>(pxWidth) * 25.4f / static_cast<float>(mmWidth);
            if (dpi <= 0.0f) return 1.0f;
            return std::clamp(dpi / 96.0f, 0.5f, 4.0f);
        }

        static float xftDpiValue() {
            if (!xDisplay) return 0.0f;

            XrmInitialize();
            char* resourceString = XResourceManagerString(xDisplay);
            if (!resourceString) return 0.0f;

            XrmDatabase db = XrmGetStringDatabase(resourceString);
            if (!db) return 0.0f;

            char* type = nullptr;
            XrmValue value{};
            float dpi = 0.0f;

            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
                dpi = std::strtof(value.addr, nullptr);
            }

            XrmDestroyDatabase(db);
            return dpi;
        }

        static std::pair<int, int> initialWindowPosition(const Size& size) {
            auto displays = getDisplays();
            const Display* display = displays.empty() ? nullptr : &displays[0];
            for (const auto& candidate : displays) {
                if (candidate.primary) {
                    display = &candidate;
                    break;
                }
            }

            if (!display) return { 0, 0 };

            int x = display->x + ((display->w - size.w) / 2);
            int y = display->y + ((display->h - size.h) / 2);
            if (x < display->x) x = display->x;
            if (y < display->y) y = display->y;
            return { x, y };
        }

        static void ensureDisplay() {
            if (xDisplay) return;
            XInitThreads();
            setlocale(LC_CTYPE, "");
            XSetLocaleModifiers("");
            xDisplay = XOpenDisplay(nullptr);
            if (!xDisplay) throw std::runtime_error("[NativeWindow] XOpenDisplay failed");
            screen = DefaultScreen(xDisplay);
            wmDeleteWindow = XInternAtom(xDisplay, "WM_DELETE_WINDOW", False);
            wmState = XInternAtom(xDisplay, "_NET_WM_STATE", False);
            wmStateFullscreen = XInternAtom(xDisplay, "_NET_WM_STATE_FULLSCREEN", False);
            wmStateMaximizedVert = XInternAtom(xDisplay, "_NET_WM_STATE_MAXIMIZED_VERT", False);
            wmStateMaximizedHorz = XInternAtom(xDisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            wmStateHidden = XInternAtom(xDisplay, "_NET_WM_STATE_HIDDEN", False);
            motifWmHints = XInternAtom(xDisplay, "_MOTIF_WM_HINTS", False);
            wmProtocols = XInternAtom(xDisplay, "WM_PROTOCOLS", False);
            wmSyncRequest = XInternAtom(xDisplay, "_NET_WM_SYNC_REQUEST", False);
            wmSyncRequestCounter = XInternAtom(xDisplay, "_NET_WM_SYNC_REQUEST_COUNTER", False);
            revFrameRequest = XInternAtom(xDisplay, "_REV_FRAME_REQUEST", False);
#if REV_HAS_XSYNC
            xsyncAvailable = XSyncQueryExtension(xDisplay, &xsyncEventBase, &xsyncErrorBase);
#endif
            inputMethod = XOpenIM(xDisplay, nullptr, nullptr, nullptr);
        }

        struct MotifHints {
            unsigned long flags = 0;
            unsigned long functions = 0;
            unsigned long decorations = 0;
            long inputMode = 0;
            unsigned long status = 0;
        };

        void applyWindowManagerHints(const Details& details) {
            if (!xDisplay || !xWindow) return;

            if (!details.decorated || details.borderless || details.nativeFrameless) {
                MotifHints hints{};
                hints.flags = 2; // MWM_HINTS_DECORATIONS
                hints.decorations = 0;
                XChangeProperty(
                    xDisplay,
                    xWindow,
                    motifWmHints,
                    motifWmHints,
                    32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&hints),
                    5
                );
            }

            if (!details.resizable) {
                XSizeHints hints{};
                hints.flags = PMinSize | PMaxSize;
                hints.min_width = details.size.w;
                hints.min_height = details.size.h;
                hints.max_width = details.size.w;
                hints.max_height = details.size.h;
                XSetWMNormalHints(xDisplay, xWindow, &hints);
            }
            else if (details.size.minW > 0 || details.size.minH > 0) {
                XSizeHints hints{};
                hints.flags = PMinSize;
                hints.min_width = details.size.minW;
                hints.min_height = details.size.minH;
                XSetWMNormalHints(xDisplay, xWindow, &hints);
            }

            if (details.fullscreen) {
                Atom states[] = { wmStateFullscreen };
                XChangeProperty(xDisplay, xWindow, wmState, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(states), 1);
            }
            else if (details.maximizeButton && details.size.w >= DisplayWidth(xDisplay, screen) && details.size.h >= DisplayHeight(xDisplay, screen)) {
                Atom states[] = { wmStateMaximizedVert, wmStateMaximizedHorz };
                XChangeProperty(xDisplay, xWindow, wmState, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(states), 2);
            }
        }

        void updateWindowState() {
            if (!xDisplay || !xWindow) return;

            Atom actualType = None;
            int actualFormat = 0;
            unsigned long itemCount = 0;
            unsigned long bytesAfter = 0;
            unsigned char* data = nullptr;

            int status = XGetWindowProperty(
                xDisplay,
                xWindow,
                wmState,
                0,
                1024,
                False,
                XA_ATOM,
                &actualType,
                &actualFormat,
                &itemCount,
                &bytesAfter,
                &data
            );

            if (status != Success || actualType != XA_ATOM || actualFormat != 32) {
                if (data) XFree(data);
                return;
            }

            bool newFullscreen = false;
            bool maxVert = false;
            bool maxHorz = false;
            bool newMinimized = false;

            Atom* states = reinterpret_cast<Atom*>(data);
            for (unsigned long i = 0; i < itemCount; ++i) {
                if (states[i] == wmStateFullscreen) newFullscreen = true;
                if (states[i] == wmStateMaximizedVert) maxVert = true;
                if (states[i] == wmStateMaximizedHorz) maxHorz = true;
                if (states[i] == wmStateHidden) newMinimized = true;
            }

            XFree(data);

            bool newMaximized = maxVert && maxHorz;

            if (newMinimized != minimized) {
                minimized = newMinimized;
                notifyEvent({ minimized ? WinEvent::Type::Minimize : WinEvent::Type::Restore });
            }

            if (newMaximized != maximized) {
                maximized = newMaximized;
                notifyEvent({ maximized ? WinEvent::Type::Maximize : WinEvent::Type::Restore });
            }

            fullscreen = newFullscreen;
        }

        void createSyncCounter() {
#if REV_HAS_XSYNC
            if (!xsyncAvailable || !xWindow) return;

            XSyncIntToValue(&syncValue, 0);
            syncCounter = XSyncCreateCounter(xDisplay, syncValue);
            if (syncCounter == None) return;

            // EWMH resize synchronization: advertise a counter the WM can
            // request during interactive resize. Rev advances it only after
            // the frame has been rendered and swapped to the GLX drawable.
            unsigned long counter = static_cast<unsigned long>(syncCounter);
            XChangeProperty(
                xDisplay,
                xWindow,
                wmSyncRequestCounter,
                XA_CARDINAL,
                32,
                PropModeReplace,
                reinterpret_cast<unsigned char*>(&counter),
                1
            );
#endif
        }

        void destroySyncCounter() {
#if REV_HAS_XSYNC
            if (!xsyncAvailable || syncCounter == None) return;
            XSyncDestroyCounter(xDisplay, syncCounter);
            syncCounter = None;
            hasPendingSync = false;
#endif
        }

        void beginSyncRequest(const XClientMessageEvent& message) {
#if REV_HAS_XSYNC
            if (!xsyncAvailable || syncCounter == None) return;

            // _NET_WM_SYNC_REQUEST carries the target 64-bit counter value in
            // data.l[2] low / data.l[3] high. Multiple requests can arrive
            // before the next draw; acknowledging the latest one after swap is
            // sufficient and prevents the WM from presenting unpainted resize
            // regions.
            XSyncIntsToValue(
                &pendingSyncValue,
                static_cast<unsigned int>(message.data.l[2]),
                static_cast<int>(message.data.l[3])
            );
            hasPendingSync = true;
            requestFrame(true);
#else
            (void)message;
#endif
        }

        void completeSyncRequest() {
#if REV_HAS_XSYNC
            if (!xsyncAvailable || syncCounter == None || !hasPendingSync) return;

            XSyncSetCounter(xDisplay, syncCounter, pendingSyncValue);
            syncValue = pendingSyncValue;
            hasPendingSync = false;
            XFlush(xDisplay);
#endif
        }

        void createInputContext() {
            if (!inputMethod || !xWindow) return;
            inputContext = XCreateIC(
                inputMethod,
                XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                XNClientWindow, xWindow,
                XNFocusWindow, xWindow,
                nullptr
            );
        }

        static unsigned int cursorShape(Appearance::Cursor cursor) {
            switch (cursor) {
                case Appearance::Cursor::Default:
                case Appearance::Cursor::Arrow: return XC_left_ptr;
                case Appearance::Cursor::Caret: return XC_xterm;
                case Appearance::Cursor::Crosshair: return XC_crosshair;
                case Appearance::Cursor::Hand: return XC_hand2;
                case Appearance::Cursor::NotAllowed: return XC_X_cursor;
                case Appearance::Cursor::ArrowsHorizontal: return XC_sb_h_double_arrow;
                case Appearance::Cursor::ArrowsVertical: return XC_sb_v_double_arrow;
                case Appearance::Cursor::ArrowsDiagonalUp: return XC_top_right_corner;
                case Appearance::Cursor::ArrowsDiagonalDown: return XC_bottom_right_corner;
                case Appearance::Cursor::ArrowsOmni: return XC_fleur;
                case Appearance::Cursor::Unset:
                default: return 0;
            }
        }

        static Key translateKey(KeySym sym) {
            switch (sym) {
                case XK_Control_L: case XK_Control_R: return Key::Ctrl;
                case XK_Shift_L: case XK_Shift_R: return Key::Shift;
                case XK_Alt_L: case XK_Alt_R: return Key::Alt;
                case XK_Super_L: case XK_Super_R: return Key::Super;
                case XK_Up: return Key::Up; case XK_Down: return Key::Down;
                case XK_Left: return Key::Left; case XK_Right: return Key::Right;
                case XK_Page_Up: return Key::PageUp; case XK_Page_Down: return Key::PageDown;
                case XK_Home: return Key::Home; case XK_End: return Key::End;
                case XK_Insert: return Key::Insert; case XK_Delete: return Key::Delete;
                case XK_F1: return Key::F1; case XK_F2: return Key::F2; case XK_F3: return Key::F3;
                case XK_F4: return Key::F4; case XK_F5: return Key::F5; case XK_F6: return Key::F6;
                case XK_F7: return Key::F7; case XK_F8: return Key::F8; case XK_F9: return Key::F9;
                case XK_F10: return Key::F10; case XK_F11: return Key::F11; case XK_F12: return Key::F12;
                case XK_Escape: return Key::Escape; case XK_space: return Key::Space;
                case XK_Tab: return Key::Tab; case XK_Return: return Key::Enter;
                case XK_BackSpace: return Key::Backspace;
                case XK_0: return Key::Num0; case XK_1: return Key::Num1; case XK_2: return Key::Num2;
                case XK_3: return Key::Num3; case XK_4: return Key::Num4; case XK_5: return Key::Num5;
                case XK_6: return Key::Num6; case XK_7: return Key::Num7; case XK_8: return Key::Num8;
                case XK_9: return Key::Num9;
                case XK_a: case XK_A: return Key::A; case XK_b: case XK_B: return Key::B;
                case XK_c: case XK_C: return Key::C; case XK_d: case XK_D: return Key::D;
                case XK_e: case XK_E: return Key::E; case XK_f: case XK_F: return Key::F;
                case XK_g: case XK_G: return Key::G; case XK_h: case XK_H: return Key::H;
                case XK_i: case XK_I: return Key::I; case XK_j: case XK_J: return Key::J;
                case XK_k: case XK_K: return Key::K; case XK_l: case XK_L: return Key::L;
                case XK_m: case XK_M: return Key::M; case XK_n: case XK_N: return Key::N;
                case XK_o: case XK_O: return Key::O; case XK_p: case XK_P: return Key::P;
                case XK_q: case XK_Q: return Key::Q; case XK_r: case XK_R: return Key::R;
                case XK_s: case XK_S: return Key::S; case XK_t: case XK_T: return Key::T;
                case XK_u: case XK_U: return Key::U; case XK_v: case XK_V: return Key::V;
                case XK_w: case XK_W: return Key::W; case XK_x: case XK_X: return Key::X;
                case XK_y: case XK_Y: return Key::Y; case XK_z: case XK_Z: return Key::Z;
                default: return Key::Unknown;
            }
        }

        static bool hasPendingEvents() {
            ensureDisplay();
            return XPending(xDisplay) > 0;
        }

        static bool hasActiveWork() {
            ensureDisplay();
            for (auto& [_, window] : windows) {
                if (!window) continue;
                if (window->dirty || window->frameQueued) return true;
#if REV_HAS_XSYNC
                if (window->hasPendingSync) return true;
#endif
            }
            return false;
        }

        static void waitForEvents(int timeoutMs = 16) {
            ensureDisplay();
            if (XPending(xDisplay) > 0) return;

            pollfd fd{};
            fd.fd = ConnectionNumber(xDisplay);
            fd.events = POLLIN;
            poll(&fd, 1, timeoutMs);
        }

        static bool pumpEvents() {
            ensureDisplay();
            // Painting can queue another frame. Bound this batch to the events
            // already pending so continuous redraws yield to application work.
            // A callback may remove queued events when destroying a window.
            const int pending = XPending(xDisplay);
            for (int processed = 0; processed < pending && XPending(xDisplay) > 0; ++processed) {
                XEvent ev{};
                XNextEvent(xDisplay, &ev);

                NativeWindow* self = nullptr;
                auto it = windows.find(ev.xany.window);
                if (it != windows.end()) self = it->second;
                if (!self) continue;

                switch (ev.type) {
                    case ClientMessage:
                        if (ev.xclient.message_type == revFrameRequest) {
                            self->frameQueued = false;
                            self->notifyEvent({ WinEvent::Type::Paint });
                        }
                        else if (ev.xclient.message_type == wmProtocols) {
                            if (static_cast<Atom>(ev.xclient.data.l[0]) == wmDeleteWindow) {
                                self->closed = true;
                                self->notifyEvent({ WinEvent::Type::Close });
                            }
                            else if (static_cast<Atom>(ev.xclient.data.l[0]) == wmSyncRequest) {
                                self->beginSyncRequest(ev.xclient);
                            }
                        }
                        break;
                    case DestroyNotify:
                        self->closed = true;
                        windows.erase(ev.xdestroywindow.window);
                        self->xWindow = 0;
                        self->handle = nullptr;
                        self->notifyEvent({ WinEvent::Type::Destroy });
                        break;
                    case FocusIn:
                        if (self->inputContext) XSetICFocus(self->inputContext);
                        self->notifyEvent({ WinEvent::Type::Focus });
                        break;
                    case FocusOut:
                        if (self->inputContext) XUnsetICFocus(self->inputContext);
                        self->notifyEvent({ WinEvent::Type::Defocus });
                        break;
                    case PropertyNotify:
                        if (ev.xproperty.atom == wmState) self->updateWindowState();
                        break;
                    case ConfigureNotify: {
                        XConfigureEvent latest = ev.xconfigure;
                        XEvent next{};
                        while (XCheckTypedWindowEvent(xDisplay, self->xWindow, ConfigureNotify, &next)) {
                            latest = next.xconfigure;
                        }

                        if (self->size.w != latest.width || self->size.h != latest.height) {
                            self->size.w = latest.width;
                            self->size.h = latest.height;
                            self->notifyEvent({ WinEvent::Type::Resize, 0, 0, self->size.w, self->size.h });
                        }
                        self->posX = latest.x;
                        self->posY = latest.y;
                        self->notifyEvent({ WinEvent::Type::Move, 0, 0, latest.x, latest.y });
                        self->requestFrame(true);
                        break;
                    }
                    case Expose:
                        if (ev.xexpose.count == 0) self->notifyEvent({ WinEvent::Type::Paint });
                        break;
                    case MotionNotify:
                        self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, ev.xmotion.x_root, ev.xmotion.y_root });
                        break;
                    case ButtonPress:
                        if (ev.xbutton.button >= Button4 && ev.xbutton.button <= 7) {
                            self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, ev.xbutton.x_root, ev.xbutton.y_root });
                            const int dx=ev.xbutton.button==6?120:ev.xbutton.button==7?-120:0;
                            const int dy=ev.xbutton.button==Button4?120:ev.xbutton.button==Button5?-120:0;
                            self->notifyEvent({ WinEvent::Type::MouseWheel, 0, 0, dx, dy });
                        } else self->notifyEvent({ WinEvent::Type::MouseButton, buttonFromX(ev.xbutton.button), Press, ev.xbutton.x_root, ev.xbutton.y_root });
                        break;
                    case ButtonRelease:
                        if (ev.xbutton.button < Button4 || ev.xbutton.button > 7)
                            self->notifyEvent({ WinEvent::Type::MouseButton, buttonFromX(ev.xbutton.button), Release, ev.xbutton.x_root, ev.xbutton.y_root });
                        break;
                    case KeyPress: {
                        KeySym sym = XLookupKeysym(&ev.xkey, 0);
                        self->notifyEvent({ WinEvent::Type::Keyboard, static_cast<uint64_t>(translateKey(sym)), 1 });

                        char text[64]{};
                        KeySym ignored{};
                        Status status = 0;
                        int len = self->inputContext
                            ? Xutf8LookupString(self->inputContext, &ev.xkey, text, sizeof(text) - 1, &ignored, &status)
                            : XLookupString(&ev.xkey, text, sizeof(text) - 1, &ignored, nullptr);

                        if (len > 0) {
                            text[len] = '\0';
                            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
                            size_t index = 0;
                            while (index < static_cast<size_t>(len)) {
                                uint32_t codepoint = 0;
                                size_t consumed = decodeUtf8(bytes + index, static_cast<size_t>(len) - index, codepoint);
                                if (consumed == 0) break;
                                self->notifyEvent({ WinEvent::Type::Character, static_cast<uint64_t>(codepoint) });
                                index += consumed;
                            }
                        }
                        break;
                    }
                    case KeyRelease: {
                        KeySym sym = XLookupKeysym(&ev.xkey, 0);
                        self->notifyEvent({ WinEvent::Type::Keyboard, static_cast<uint64_t>(translateKey(sym)), 0 });
                        break;
                    }
                }
            }
            return true;
        }

        static size_t decodeUtf8(const unsigned char* input, size_t length, uint32_t& codepoint) {
            if (length == 0) return 0;

            unsigned char c = input[0];
            if (c < 0x80) {
                codepoint = c;
                return 1;
            }
            if ((c & 0xE0) == 0xC0 && length >= 2) {
                codepoint = ((c & 0x1F) << 6) | (input[1] & 0x3F);
                return 2;
            }
            if ((c & 0xF0) == 0xE0 && length >= 3) {
                codepoint = ((c & 0x0F) << 12) | ((input[1] & 0x3F) << 6) | (input[2] & 0x3F);
                return 3;
            }
            if ((c & 0xF8) == 0xF0 && length >= 4) {
                codepoint = ((c & 0x07) << 18) | ((input[1] & 0x3F) << 12) | ((input[2] & 0x3F) << 6) | (input[3] & 0x3F);
                return 4;
            }

            codepoint = c;
            return 1;
        }

        static uint64_t buttonFromX(unsigned int button) {
            switch (button) {
                case Button1: return Left;
                case Button2: return Middle;
                case Button3: return Right;
                default: return Left;
            }
        }
    };
}
