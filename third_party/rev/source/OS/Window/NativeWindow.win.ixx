module;

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Cpp
#include <stdexcept>
#include <functional>
#include <unordered_map>

// Win32
#include <windowsx.h>
#include <windows.h>
#include <shellscalingapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>

// Misc
#include <glew/glew.h>
#include <dbg.hpp>
#include <rev_win_message_pump.hpp>

#include "WinEvent.hpp"

export module Rev.NativeWindow;

import Rev.Appearance;

export namespace Rev {

    struct NativeWindow {

        enum ButtonAction {
            Release, Press, DoubleClick
        };

        enum MouseButton {
            Left, Right, Middle
        };

        // Keycode mapping
        //--------------------------------------------------
        
        // Unified key enum
        enum class Key : int {
            
            Unknown,

            Ctrl, Shift, Alt, Super,

            // Navigation
            Up, Down, Left, Right,
            PageUp, PageDown, Home, End, Insert, Delete,

            // Function keys
            F1, F2, F3, F4, F5, F6,
            F7, F8, F9, F10, F11, F12,

            // Numbers (top row)
            Num0, Num1, Num2, Num3, Num4,
            Num5, Num6, Num7, Num8, Num9,

            // Letters
            A, B, C, D, E, F, G, H, I, J,
            K, L, M, N, O, P, Q, R, S, T,
            U, V, W, X, Y, Z,

            // Numpad
            Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
            Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
            NumpadAdd, NumpadSub, NumpadMul, NumpadDiv, NumpadEnter, NumpadDecimal,

            // Misc
            Escape, Space, Tab, Enter, Backspace,
            CapsLock, NumLock, ScrollLock,
            PrintScreen, Pause,
        };

        // Win32 keycode -> key enum map
        inline static const std::unordered_map<int, Key> WinKeys = {

            { VK_CONTROL, Key::Ctrl },
            { VK_LCONTROL, Key::Ctrl },
            { VK_RCONTROL, Key::Ctrl },
            { VK_SHIFT,   Key::Shift },
            { VK_LSHIFT,  Key::Shift },
            { VK_RSHIFT,  Key::Shift },
            { VK_MENU,    Key::Alt },
            { VK_LMENU,   Key::Alt },
            { VK_RMENU,   Key::Alt },
            { VK_LWIN,    Key::Super }, { VK_RWIN, Key::Super },
        
            { VK_UP,    Key::Up }, { VK_DOWN,  Key::Down },
            { VK_LEFT,  Key::Left }, { VK_RIGHT, Key::Right },
        
            { VK_PRIOR, Key::PageUp }, { VK_NEXT,  Key::PageDown },
            { VK_HOME,  Key::Home }, { VK_END,   Key::End },
            { VK_INSERT,Key::Insert }, { VK_DELETE,Key::Delete },
        
            { VK_F1,  Key::F1 }, { VK_F2,  Key::F2 }, { VK_F3,  Key::F3 },
            { VK_F4,  Key::F4 }, { VK_F5,  Key::F5 }, { VK_F6,  Key::F6 },
            { VK_F7,  Key::F7 }, { VK_F8,  Key::F8 }, { VK_F9,  Key::F9 },
            { VK_F10, Key::F10 },{ VK_F11, Key::F11 },{ VK_F12, Key::F12 },
        
            { '0', Key::Num0 }, { '1', Key::Num1 }, { '2', Key::Num2 },
            { '3', Key::Num3 }, { '4', Key::Num4 }, { '5', Key::Num5 },
            { '6', Key::Num6 }, { '7', Key::Num7 }, { '8', Key::Num8 }, { '9', Key::Num9 },
        
            { 'A', Key::A }, { 'B', Key::B }, { 'C', Key::C }, { 'D', Key::D }, { 'E', Key::E },
            { 'F', Key::F }, { 'G', Key::G }, { 'H', Key::H }, { 'I', Key::I }, { 'J', Key::J },
            { 'K', Key::K }, { 'L', Key::L }, { 'M', Key::M }, { 'N', Key::N }, { 'O', Key::O },
            { 'P', Key::P }, { 'Q', Key::Q }, { 'R', Key::R }, { 'S', Key::S }, { 'T', Key::T },
            { 'U', Key::U }, { 'V', Key::V }, { 'W', Key::W }, { 'X', Key::X }, { 'Y', Key::Y },
            { 'Z', Key::Z },
        
            { VK_NUMPAD0, Key::Numpad0 }, { VK_NUMPAD1, Key::Numpad1 }, { VK_NUMPAD2, Key::Numpad2 },
            { VK_NUMPAD3, Key::Numpad3 }, { VK_NUMPAD4, Key::Numpad4 }, { VK_NUMPAD5, Key::Numpad5 },
            { VK_NUMPAD6, Key::Numpad6 }, { VK_NUMPAD7, Key::Numpad7 }, { VK_NUMPAD8, Key::Numpad8 },
            { VK_NUMPAD9, Key::Numpad9 },

            { VK_ADD, Key::NumpadAdd }, { VK_SUBTRACT, Key::NumpadSub },
            { VK_MULTIPLY, Key::NumpadMul }, { VK_DIVIDE, Key::NumpadDiv },
            { VK_RETURN, Key::NumpadEnter }, { VK_DECIMAL, Key::NumpadDecimal },
        
            { VK_ESCAPE,    Key::Escape },
            { VK_SPACE,     Key::Space },
            { VK_TAB,       Key::Tab },
            { VK_RETURN,    Key::Enter },
            { VK_BACK,      Key::Backspace },
            { VK_CAPITAL,   Key::CapsLock },
            { VK_NUMLOCK,   Key::NumLock },
            { VK_SCROLL,    Key::ScrollLock },
            { VK_SNAPSHOT,  Key::PrintScreen },
            { VK_PAUSE,     Key::Pause },
        };

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) {

            switch (key) {

                case Key::Ctrl:   return "Ctrl";
                case Key::Shift:  return "Shift";
                case Key::Alt:    return "Alt";
                case Key::Super:  return "Super";
        
                case Key::Up:    return "Up";
                case Key::Down:  return "Down";
                case Key::Left:  return "Left";
                case Key::Right: return "Right";
        
                case Key::PageUp:   return "PageUp";
                case Key::PageDown: return "PageDown";
                case Key::Home:     return "Home";
                case Key::End:      return "End";
                case Key::Insert:   return "Insert";
                case Key::Delete:   return "Delete";
        
                case Key::F1:  return "F1";
                case Key::F2:  return "F2";
                case Key::F3:  return "F3";
                case Key::F4:  return "F4";
                case Key::F5:  return "F5";
                case Key::F6:  return "F6";
                case Key::F7:  return "F7";
                case Key::F8:  return "F8";
                case Key::F9:  return "F9";
                case Key::F10: return "F10";
                case Key::F11: return "F11";
                case Key::F12: return "F12";
        
                case Key::A: return "A";
                case Key::B: return "B";
                case Key::C: return "C";
                // … repeat for all alphas/numbers …
        
                case Key::Escape:    return "Escape";
                case Key::Space:     return "Space";
                case Key::Tab:       return "Tab";
                case Key::Enter:     return "Enter";
                case Key::Backspace: return "Backspace";
        
                case Key::CapsLock:  return "CapsLock";
                case Key::NumLock:   return "NumLock";
                case Key::ScrollLock:return "ScrollLock";
        
                case Key::PrintScreen: return "PrintScreen";
                case Key::Pause:       return "Pause";
        
                default: return "Unknown";
            }
        }

        // Monitor Info
        //--------------------------------------------------

        struct Display {

            HMONITOR handle;

            std::string friendlyName;

            int x, y, w, h;

            bool primary = false;
        };

        static std::wstring widen(const std::string& utf8) {

            if (utf8.empty()) {
                return L"";
            }

            int size = MultiByteToWideChar(
                CP_UTF8,
                0,
                utf8.c_str(),
                (int)utf8.size(),
                nullptr,
                0
            );

            std::wstring result(size, 0);

            MultiByteToWideChar(
                CP_UTF8,
                0,
                utf8.c_str(),
                (int)utf8.size(),
                result.data(),
                size
            );

            return result;
        }

        static std::string narrow(const std::wstring& wide) {

            if (wide.empty()) {
                return "";
            }

            int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                (int)wide.size(),
                nullptr,
                0,
                nullptr,
                nullptr
            );

            std::string result(size, 0);

            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                (int)wide.size(),
                result.data(),
                size,
                nullptr,
                nullptr
            );

            return result;
        }

        static std::string getFriendlyName(HMONITOR monitor) {

            MONITORINFOEXW monInfo = {};
            monInfo.cbSize = sizeof(monInfo);

            if (!GetMonitorInfoW(monitor, &monInfo)) {
                return "";
            }

            UINT32 pathCount = 0;
            UINT32 modeCount = 0;

            if (GetDisplayConfigBufferSizes(
                QDC_ONLY_ACTIVE_PATHS,
                &pathCount,
                &modeCount
            ) != ERROR_SUCCESS)
            {
                return "";
            }

            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

            if (QueryDisplayConfig(
                QDC_ONLY_ACTIVE_PATHS,
                &pathCount,
                paths.data(),
                &modeCount,
                modes.data(),
                nullptr
            ) != ERROR_SUCCESS)
            {
                return "";
            }

            for (const auto& path : paths) {

                // Query SOURCE name
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};

                source.header.type =
                    DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;

                source.header.size = sizeof(source);

                source.header.adapterId =
                    path.sourceInfo.adapterId;

                source.header.id =
                    path.sourceInfo.id;

                if (DisplayConfigGetDeviceInfo(&source.header)
                    != ERROR_SUCCESS)
                {
                    continue;
                }

                // Match this path to the monitor
                if (wcscmp(
                    source.viewGdiDeviceName,
                    monInfo.szDevice
                ) != 0)
                {
                    continue;
                }

                // Query TARGET name
                DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};

                target.header.type =
                    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;

                target.header.size = sizeof(target);

                target.header.adapterId =
                    path.targetInfo.adapterId;

                target.header.id =
                    path.targetInfo.id;

                if (DisplayConfigGetDeviceInfo(&target.header)
                    != ERROR_SUCCESS)
                {
                    continue;
                }

                std::wstring wide =
                    target.monitorFriendlyDeviceName;

                int size = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide.c_str(),
                    (int)wide.size(),
                    nullptr,
                    0,
                    nullptr,
                    nullptr
                );

                std::string result(size, 0);

                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide.c_str(),
                    (int)wide.size(),
                    result.data(),
                    size,
                    nullptr,
                    nullptr
                );

                return result;
            }

            return "";
        }

        static std::vector<Display> getDisplays() {

            std::vector<Display> displays;

            EnumDisplayMonitors(
                nullptr,
                nullptr,
                [](HMONITOR hMon, HDC, LPRECT, LPARAM user) -> BOOL {

                    auto* out =
                        reinterpret_cast<std::vector<Display>*>(user);

                    MONITORINFOEXW info = {};
                    info.cbSize = sizeof(info);

                    GetMonitorInfoW(hMon, &info);

                    std::string friendly =
                        getFriendlyName(hMon);

                    out->push_back({
                        .handle = hMon,
                        .friendlyName = friendly,

                        .x = info.rcMonitor.left,
                        .y = info.rcMonitor.top,

                        .w = info.rcMonitor.right - info.rcMonitor.left,
                        .h = info.rcMonitor.bottom - info.rcMonitor.top,

                        .primary =
                            bool(info.dwFlags & MONITORINFOF_PRIMARY)
                    });

                    return TRUE;
                },
                (LPARAM)&displays
            );

            return displays;
        }

        // Own data
        //--------------------------------------------------

        struct Size {
            int w, h, minW, minH, maxW, maxH;
        };

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

            // No OS title bar, but a native resizable frame. See WM_NCCALCSIZE /
            // WM_NCHITTEST below.
            bool nativeFrameless = false;
        };

        enum class Relationship {
            Independent,
            OwnedTopLevel,
            EmbeddedChild
        };

        int posX, posY;

        using EventCallback = std::function<void(WinEvent&)>;
        EventCallback callback;

        static constexpr LPCWSTR kClassName = L"Room360RawViewWindow";

        inline static HGLRC sharedRoot = nullptr;
        HWND handle = nullptr;

        // Window icons (alt-tab/taskbar + title bar), owned here and destroyed
        // in the destructor. See setIcon().
        HICON hIconBig = nullptr;
        HICON hIconSmall = nullptr;
        
        Size size;
        float scale = 1.0f;

        Appearance::Cursor cursor;

        bool dirty = false;
        bool invalidatePending = false;
        bool destroyed = false;

        // Number of mouse buttons currently held. Used to drive mouse capture:
        // capture on the first press, release on the last release. Capture keeps
        // move / button-up messages flowing here even when the cursor leaves the
        // client rect (drag continuity beyond the window edge).
        int buttonsDown = 0;
        Relationship relationship = Relationship::EmbeddedChild;

        // Frameless-but-native-resizable window (no title bar, WS_THICKFRAME kept).
        bool nativeFrameless = false;

        inline static NativeWindow* currentWindow = nullptr;
        inline static NativeWindow* resourceRoot = nullptr;

        bool isEphemeral() const {
            return relationship == Relationship::OwnedTopLevel;
        }

        struct ResourceContextGuard {

            NativeWindow* owner = nullptr;
            NativeWindow* previousWindow = nullptr;
            bool switched = false;

            ResourceContextGuard(NativeWindow* resourceOwner)
                : owner(resourceOwner)
                , previousWindow(currentWindow)
            {
                if (owner && !owner->isContextCurrent()) {
                    switched = owner->tryMakeContextCurrent();
                }
            }

            ~ResourceContextGuard() {

                if (!switched) {
                    return;
                }

                if (previousWindow) {
                    previousWindow->tryMakeContextCurrent();
                    return;
                }

                wglMakeCurrent(nullptr, nullptr);
                currentWindow = nullptr;
            }
        };

        static void requireContext(void* ownerContext, const char* operation) {

            NativeWindow* owner = static_cast<NativeWindow*>(ownerContext);
            if (!owner) {
                return;
            }

            if (owner->isContextCurrent()) {
                return;
            }

            if (owner->tryMakeContextCurrent()) {
                return;
            }

            throw std::runtime_error(
                std::string("[GL] Failed to make resource context current before ")
                + operation
            );
        }

        // False requests orderly application shutdown after WM_QUIT.
        static bool pumpEvents() { return RevWinMessagePump::pump(); }

        NativeWindow(
            void* ownerOrParent,
            Details details,
            EventCallback callback = nullptr,
            Relationship relationship = Relationship::EmbeddedChild
        ) {
            
            this->size = details.size;
            this->callback = callback;
            this->relationship = relationship;
            this->nativeFrameless = details.nativeFrameless;

            // --------------------------------------------------
            // Enable Per-Monitor DPI Awareness once per process
            // --------------------------------------------------
            static bool dpiAwareSet = false;
            if (!dpiAwareSet) {

                dpiAwareSet = true;

                // Prefer modern API if available (Win10+)
                if (SetProcessDpiAwarenessContext) {
                    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                }
                
                else {
                    // Fallback (Win8.1)
                    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
                    if (shcore) {
                        using SetProcDpiAwareFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
                        auto fn = reinterpret_cast<SetProcDpiAwareFn>(
                            GetProcAddress(shcore, "SetProcessDpiAwareness"));
                        if (fn) fn(PROCESS_PER_MONITOR_DPI_AWARE);
                        FreeLibrary(shcore);
                    } else {
                        SetProcessDPIAware(); // legacy fallback
                    }
                }
            }

            // Register window class
            //--------------------------------------------------

            WNDCLASSW wc = {
                .style          = CS_HREDRAW | CS_VREDRAW,
                .lpfnWndProc    = eventHandler,
                .hInstance      = GetModuleHandleW(nullptr),
                .hCursor        = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW),
                .hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1),
                .lpszClassName  = kClassName
            };

            RegisterClassW(&wc);

            // Register window
            //--------------------------------------------------
            
            HWND relatedHwnd = static_cast<HWND>(ownerOrParent);
            HWND createParent = nullptr;
            DWORD exStyle = 0;
            DWORD style = WS_VISIBLE;

            if (relationship == Relationship::EmbeddedChild && relatedHwnd) {
                createParent = relatedHwnd;
                style |= WS_CHILD;
            }

            else {
                if (relationship == Relationship::OwnedTopLevel) {
                    createParent = relatedHwnd;
                }

                if (details.nativeFrameless) {
                    // Keep the full overlapped style so WS_THICKFRAME (native
                    // resize + Aero snap), WS_CAPTION (drop shadow / snap layout)
                    // and the min/max boxes survive. The visible frame + title bar
                    // are then suppressed in WM_NCCALCSIZE, and resize hit zones
                    // are reported from WM_NCHITTEST.
                    style |= WS_OVERLAPPEDWINDOW;
                }

                else if (details.borderless || !details.decorated) {
                    style = WS_POPUP | WS_VISIBLE;
                }

                else {
                    style |= WS_OVERLAPPEDWINDOW;

                    if (!details.resizable) {
                        style &= ~WS_THICKFRAME;
                    }

                    if (!details.closeButton) {
                        style &= ~WS_SYSMENU;
                    }

                    if (!details.minimizeButton) {
                        style &= ~WS_MINIMIZEBOX;
                    }

                    if (!details.maximizeButton) {
                        style &= ~WS_MAXIMIZEBOX;
                    }
                }
            }

            // When we ask for a size, the resulting window size includes the top bar, etc.
            // We don't want that
            RECT rect = { 0, 0, details.size.w, details.size.h };
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);

            struct CreatingScope {
                CreatingScope(NativeWindow* window) { creatingWindow() = window; }
                ~CreatingScope() { creatingWindow() = nullptr; }
            } creatingScope(this);

            handle = CreateWindowExW(
                exStyle, kClassName, widen(details.name).c_str(),
                style,
                CW_USEDEFAULT, CW_USEDEFAULT,
                rect.right - rect.left,
                rect.bottom - rect.top,
                createParent, nullptr, GetModuleHandle(nullptr),
                this
            );

            if (!handle) {
                throw std::runtime_error("[NativeWindow] Failed to create window");
            }

            // Some style paths finish non-client initialization after
            // CreateWindowExW returns; make the requested caption explicit once
            // the HWND is fully available.
            setTitle(details.name);

            // Frameless: force a non-client recalc so WM_NCCALCSIZE collapses the
            // title bar/frame right away (otherwise it can flash on first paint).
            if (nativeFrameless) {
                SetWindowPos(
                    handle, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
                );
            }

            // Set scale and notify parent
            //--------------------------------------------------

            UINT dpi = GetDpiForWindow(handle);
            scale = dpi / 96.0f; // 96 DPI = 100% scaling

            this->notifyEvent({ WinEvent::Type::Scale });
            this->notifyEvent({ WinEvent::Type::Resize, 0, 0, details.size.w, details.size.h });
        }

        void show() {

            if (!handle) { return; }

            ShowWindow(handle, SW_SHOW);
            UpdateWindow(handle);
            SetForegroundWindow(handle);
        }

        // Set the window icon from raw RGBA8 pixels (top row first). `big`
        // selects the alt-tab/taskbar icon (ICON_BIG, ~32px) vs the title-bar
        // icon (ICON_SMALL, ~16px); call once per size. The caller rasterizes at
        // the desired size (e.g. via Rev::Core::Svg::rasterize) and owns its
        // pixels -- we copy into a GDI bitmap. The previous HICON of that slot is
        // destroyed here, and both are destroyed in the destructor.
        void setIcon(const unsigned char* rgba, int w, int h, bool big) {

            if (!handle || !rgba || w <= 0 || h <= 0) { return; }

            // 32-bit top-down BGRA DIB for the colour plane. nanosvg gives RGBA,
            // Windows DIBs are BGRA, so swap R<->B on copy. Negative height makes
            // the DIB top-down to match the incoming pixel order.
            BITMAPV5HEADER bi = {};
            bi.bV5Size        = sizeof(BITMAPV5HEADER);
            bi.bV5Width       = w;
            bi.bV5Height      = -h;
            bi.bV5Planes      = 1;
            bi.bV5BitCount    = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask     = 0x00FF0000;
            bi.bV5GreenMask   = 0x0000FF00;
            bi.bV5BlueMask    = 0x000000FF;
            bi.bV5AlphaMask   = 0xFF000000;

            void* dibPixels = nullptr;
            HDC dc = GetDC(nullptr);
            HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
            ReleaseDC(nullptr, dc);

            if (!color || !dibPixels) {
                if (color) { DeleteObject(color); }
                return;
            }

            unsigned char* dst = static_cast<unsigned char*>(dibPixels);
            for (int i = 0; i < w * h; i++) {
                dst[i * 4 + 0] = rgba[i * 4 + 2];   // B
                dst[i * 4 + 1] = rgba[i * 4 + 1];   // G
                dst[i * 4 + 2] = rgba[i * 4 + 0];   // R
                dst[i * 4 + 3] = rgba[i * 4 + 3];   // A
            }

            // A mask bitmap is required by ICONINFO; the alpha channel does the
            // real compositing, so an empty (all-zero) mask is fine.
            HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);

            ICONINFO ii = {};
            ii.fIcon    = TRUE;
            ii.hbmColor = color;
            ii.hbmMask  = mask;

            HICON icon = CreateIconIndirect(&ii);

            DeleteObject(color);
            DeleteObject(mask);

            if (!icon) { return; }

            HICON& slot = big ? hIconBig : hIconSmall;
            if (slot) { DestroyIcon(slot); }
            slot = icon;

            SendMessageW(handle, WM_SETICON, big ? ICON_BIG : ICON_SMALL, (LPARAM)icon);
        }

        ~NativeWindow() {
            //dbg("[NativeWindow] destroying");

            if (hIconBig)   { DestroyIcon(hIconBig);   hIconBig = nullptr; }
            if (hIconSmall) { DestroyIcon(hIconSmall); hIconSmall = nullptr; }

            if (hglrc) {
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(hglrc);
                hglrc = nullptr;
            }
        
            if (hdc && handle) {
                ReleaseDC(handle, hdc);
                hdc = nullptr;
            }
        
            if (handle) {
                if (!destroyed) {
                    DestroyWindow(handle);
                }

                handle = nullptr;
            }
        }

        void setSize(int w, int h) {
            
            this->size.w = w;
            this->size.h = h;
            
            // The public size is the physical client extent on every platform.
            // Passing it as the outer extent shrinks decorated clients and can
            // recursively trigger a minimum-client-size clamp from WM_SIZE.
            POINT outer = { w, h };
            applyClientTrackSize(handle, w, h, outer);
            SetWindowPos(handle, nullptr, 0, 0, outer.x, outer.y, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        }

        void setPos(int x, int y) {

            posX = x;
            posY = y;

            SetWindowPos(
                handle,
                nullptr,
                x,
                y,
                0,
                0,
                SWP_NOZORDER |
                SWP_NOACTIVATE |
                SWP_NOSIZE
            );
        }

        // Actual window top-left in screen (physical) pixels — the window origin,
        // including any OS border/title bar, as opposed to the client origin
        // reported by WM_MOVE. Used to pin a drag so a decorated window doesn't
        // jump by the frame offset when repositioned via SetWindowPos.
        void getWindowPos(int& x, int& y) const {

            if (!handle) {
                x = posX;
                y = posY;
                return;
            }

            RECT rect;
            GetWindowRect(handle, &rect);

            x = rect.left;
            y = rect.top;
        }

        // Screen (physical) position of the client area's top-left corner, queried
        // live from the OS. This is the authoritative origin for converting an
        // absolute mouse screen position into window-local coordinates — unlike a
        // cached details.x/y, it is always correct regardless of how the window was
        // last moved (native drag, our drag, SetWindowPos, multi-monitor, etc.).
        void getClientPos(int& x, int& y) const {

            POINT origin = { 0, 0 };

            if (handle) {
                ClientToScreen(handle, &origin);
            }

            x = origin.x;
            y = origin.y;
        }

        void setTitle(const std::string& title) {

            if (!handle) {
                return;
            }

            SetWindowTextW(handle, widen(title).c_str());
        }

        void setRect(int x, int y, int w, int h) {

            posX = x;
            posY = y;

            size.w = w;
            size.h = h;

            SetWindowPos(
                handle,
                nullptr,
                x,
                y,
                w,
                h,
                SWP_NOZORDER |
                SWP_NOACTIVATE
            );
        }

        void setCursor(Appearance::Cursor newCursor) {

            // Return if no handle or no cursor change
            if (!handle || cursor == newCursor)
                return;

            cursor = newCursor; // cache

            // Hidden cursor: clear the class cursor and hide it.
            if (newCursor == Appearance::Cursor::None) {
                SetClassLongPtrW(handle, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(nullptr));
                SetCursor(nullptr);
                return;
            }

            LPCWSTR cursorId = IDC_ARROW; // default fallback
        
            // Directly match enum to Win32 cursor
            switch (newCursor) {
        
                case Appearance::Cursor::Unset:
                case Appearance::Cursor::Default:
                case Appearance::Cursor::Arrow:
                    cursorId = IDC_ARROW;
                    break;
        
                case Appearance::Cursor::Caret:
                    cursorId = IDC_IBEAM;
                    break;
        
                case Appearance::Cursor::Crosshair:
                    cursorId = IDC_CROSS;
                    break;
        
                case Appearance::Cursor::Hand:
                    cursorId = IDC_HAND;
                    break;
        
                case Appearance::Cursor::NotAllowed:
                    cursorId = IDC_NO;
                    break;
        
                case Appearance::Cursor::ArrowsHorizontal:
                    cursorId = IDC_SIZEWE;
                    break;
        
                case Appearance::Cursor::ArrowsVertical:
                    cursorId = IDC_SIZENS;
                    break;
        
                case Appearance::Cursor::ArrowsDiagonalUp:
                    cursorId = IDC_SIZENESW;
                    break;
        
                case Appearance::Cursor::ArrowsDiagonalDown:
                    cursorId = IDC_SIZENWSE;
                    break;
        
                case Appearance::Cursor::ArrowsOmni:
                    cursorId = IDC_SIZEALL;
                    break;
        
                default:
                    cursorId = IDC_ARROW;
                    break;
            }
        
            // Load and set the Win32 cursor
            HCURSOR hCur = LoadCursorW(nullptr, cursorId);
        
            // Update class cursor (for when mouse re-enters)
            SetClassLongPtrW(handle, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(hCur));
        
            // Immediately update the visible cursor
            SetCursor(hCur);
        }
        

        void requestFrame() {

            if (dirty) {
                invalidatePending = true;
                return;
            }

            dirty = true;

            InvalidateRect(handle, nullptr, FALSE);
        }

        void flushPendingInvalidate() {

            if (!invalidatePending) { return; }

            invalidatePending = false;

            InvalidateRect(handle, nullptr, FALSE);
        }

        // Notify listener callback of event
        WinEvent notifyEvent(WinEvent event) {

            event.subject = this;

            // NOTE: mouse coordinates are delivered as absolute *screen* pixels
            // (physical, unscaled). Window converts to window-local logical
            // coordinates using its own screen origin and DPI scale. We no longer
            // divide here — screen space must stay in physical px so it lines up
            // with SetWindowPos / setPos for window drag + resize.
            if (callback) { callback(event); }
            
            return event;
        }
        
        // Translate Win32 keycode to unified Key enum
        Key getKey(WPARAM wp) {
            auto it = WinKeys.find(static_cast<int>(wp));
            return (it != WinKeys.end()) ? it->second : Key::Unknown;
        }

        // Win32 routes Alt (and other system combos) through WM_SYSKEY* instead of WM_KEY*.
        bool notifyKeyboard(WPARAM wp, int action) {

            Key key = getKey(wp);

            if (key == Key::Unknown) {
                return false;
            }

            notifyEvent({
                WinEvent::Type::Keyboard,
                static_cast<uint64_t>(key),
                static_cast<uint64_t>(action)
            });

            return true;
        }

        // Mouse button + capture management.
        //--------------------------------------------------

        // Convert the client-relative coordinates in lParam to absolute screen
        // (physical) pixels, then notify a MouseButton event. Capture on the
        // first button down so the matching up + any drag-out moves still arrive.
        void onButtonDown(HWND h, int button, LPARAM lp) {

            if (buttonsDown++ == 0) {
                SetCapture(h);
            }

            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(h, &pt);

            notifyEvent({ WinEvent::Type::MouseButton, (uint64_t)button, 1, pt.x, pt.y });
        }

        void onButtonUp(HWND h, int button, LPARAM lp) {

            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(h, &pt);

            notifyEvent({ WinEvent::Type::MouseButton, (uint64_t)button, 0, pt.x, pt.y });

            // Release only once the last held button comes up. Our own
            // ReleaseCapture posts WM_CAPTURECHANGED, but by then buttonsDown is
            // already 0, so the handler there treats it as a no-op.
            if (buttonsDown > 0 && --buttonsDown == 0) {
                ReleaseCapture();
            }
        }

        // Get self (user pointer) from window handle
        static NativeWindow* Self(HWND h) {
            return reinterpret_cast<NativeWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        }

        // WM_GETMINMAXINFO can arrive before WM_NCCREATE stores GWLP_USERDATA.
        static NativeWindow*& creatingWindow() {
            thread_local NativeWindow* pending = nullptr;
            return pending;
        }

        static void applyClientTrackSize(HWND h, int clientW, int clientH, POINT& track) {
            if (clientW <= 0 || clientH <= 0) { return; }

            NativeWindow* win = Self(h);
            if (!win) { win = creatingWindow(); }
            if (win && win->nativeFrameless) {
                // Match our WM_NCCALCSIZE override: the normal client covers
                // the complete outer rectangle; maximized clients keep only
                // the frame insets used by that override.
                int fx = IsZoomed(h) ? GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER) : 0;
                int fy = IsZoomed(h) ? GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER) : 0;
                track = { clientW + 2 * fx, clientH + 2 * fy };
                return;
            }

            DWORD style = GetWindowLongW(h, GWL_STYLE);
            DWORD exStyle = GetWindowLongW(h, GWL_EXSTYLE);
            RECT rect = { 0, 0, clientW, clientH };

            using AdjustForDpi = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
            static auto adjustForDpi = reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
            const UINT dpi = GetDpiForWindow(h);
            if (adjustForDpi && dpi) {
                if (!adjustForDpi(&rect, style, FALSE, exStyle, dpi)) { return; }
            } else if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle)) { return; }

            track.x = rect.right - rect.left;
            track.y = rect.bottom - rect.top;
        }

        // Static event handler function passed to the Win32 api - captures and sends events back to self
        static LRESULT CALLBACK eventHandler(HWND h, UINT msg, WPARAM wp, LPARAM lp) {

            // Retrieve instance for all other messages
            NativeWindow* self = Self(h);

            switch (msg) {

                // When the window is first created
                case (WM_NCCREATE): {

                    CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                    NativeWindow* self = static_cast<NativeWindow*>(cs->lpCreateParams);

                    // Store own self pointer 
                    if (self) {
                        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                        self->handle = h;
                    }

                    self->notifyEvent({
                        WinEvent::Type::Create
                    });
                    
                    return TRUE; // tell Windows creation may continue
                }

                case (WM_CLOSE): {

                    WinEvent result = self->notifyEvent({
                        WinEvent::Type::Close
                    });

                    if (result.rejected) {
                        return 0;
                    }

                    // Do not DestroyWindow here. GL resources must be deleted
                    // from ~Window while this HWND and HDC are still valid.
                    // Application::run deletes the Window when shouldClose is set;
                    // ~NativeWindow destroys the HWND after GL teardown.
                    return 0;
                }

                // When the window is destroyed
                case (WM_DESTROY): {

                    self->destroyed = true;
                    
                    self->notifyEvent({
                        WinEvent::Type::Destroy
                    });
                    
                    return 0;
                }

                // Frameless: suppress the OS title bar / frame by reclaiming the
                // ENTIRE non-client area into the client (full-bleed). We must NOT
                // leave an inset ring here: any non-client strip we don't cover
                // is left unpainted by GL and shows through to whatever is behind
                // the window (and hit-tests as frame, not content). The visible
                // 1px frame is drawn by the engine instead (see Window). When
                // maximized we inset by the real frame metrics so the window
                // doesn't spill over the taskbar / adjacent monitors.
                case (WM_NCCALCSIZE): {

                    if (wp == TRUE && self && self->nativeFrameless) {

                        if (IsZoomed(h)) {

                            NCCALCSIZE_PARAMS* params =
                                reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);

                            RECT& rc = params->rgrc[0];

                            int fx = GetSystemMetrics(SM_CXFRAME)
                                   + GetSystemMetrics(SM_CXPADDEDBORDER);
                            int fy = GetSystemMetrics(SM_CYFRAME)
                                   + GetSystemMetrics(SM_CXPADDEDBORDER);

                            rc.left   += fx;
                            rc.right  -= fx;
                            rc.top    += fy;
                            rc.bottom -= fy;
                        }

                        // Normal: leave rgrc[0] as the full window rect — client
                        // covers everything, no title bar, no unpainted gap.
                        return 0;
                    }

                    break;
                }

                // Frameless: report resize zones along the (now invisible) edges so
                // DefWindowProc performs native resize / snap. No HTCAPTION is
                // returned, so the window is resize-only for now (move was the old
                // manual logic, intentionally dropped).
                case (WM_NCHITTEST): {

                    if (self && self->nativeFrameless) {

                        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

                        RECT r;
                        GetWindowRect(h, &r);

                        // Grab band wider than the visible 1px so it's usable.
                        int grip = (int)(8 * self->scale);
                        if (grip < 4) { grip = 4; }

                        bool left   = pt.x <  r.left   + grip;
                        bool right  = pt.x >= r.right  - grip;
                        bool top    = pt.y <  r.top    + grip;
                        bool bottom = pt.y >= r.bottom - grip;

                        if (top && left)     { return HTTOPLEFT; }
                        if (top && right)    { return HTTOPRIGHT; }
                        if (bottom && left)  { return HTBOTTOMLEFT; }
                        if (bottom && right) { return HTBOTTOMRIGHT; }
                        if (left)            { return HTLEFT; }
                        if (right)           { return HTRIGHT; }
                        if (top)             { return HTTOP; }
                        if (bottom)          { return HTBOTTOM; }

                        return HTCLIENT;
                    }

                    break;
                }

                // When the window gains focus
                case (WM_SETFOCUS): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Focus
                    });
                    
                    return 0;
                }

                // When the window loses focus
                case (WM_KILLFOCUS): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Defocus
                    });
                    
                    return 0;
                }

                case (WM_GETMINMAXINFO): {

                    DefWindowProcW(h, msg, wp, lp);

                    NativeWindow* win = self ? self : creatingWindow();
                    if (!win) { return 0; }

                    MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);

                    if (win->size.minW > 0 && win->size.minH > 0) {
                        applyClientTrackSize(h, win->size.minW, win->size.minH, mmi->ptMinTrackSize);
                    }

                    if (win->size.maxW > 0 && win->size.maxH > 0) {
                        applyClientTrackSize(h, win->size.maxW, win->size.maxH, mmi->ptMaxTrackSize);
                    }

                    return 0;
                }

                // When the window is resized
                case (WM_SIZE): {

                    self->size.w = LOWORD(lp);
                    self->size.h = HIWORD(lp);

                    self->notifyEvent({
                        WinEvent::Type::Resize,
                        0, 0,
                        LOWORD(lp), HIWORD(lp)
                    });

                    switch (wp) {
                        case (SIZE_MINIMIZED): { self->notifyEvent({ WinEvent::Type::Minimize }); break; }
                        case (SIZE_MAXIMIZED): { self->notifyEvent({ WinEvent::Type::Maximize }); break; }
                    }

                    // Frameless full-bleed clients "rubber band" while resizing:
                    // the OS modal resize loop blocks our main loop, so WM_PAINT is
                    // starved and DWM stretches the previous frame to the new size
                    // until a repaint finally lands. The Resize notify above already
                    // invalidated; flush it synchronously here so the content is
                    // redrawn at the new size on this very message, tracking the
                    // frame edge smoothly. Gated on hglrc so it never fires before
                    // the GL context / canvas exist (e.g. the creation-time WM_SIZE).
                    if (self->nativeFrameless && self->hglrc) {
                        UpdateWindow(h);
                    }

                    return 0;
                }

                case (WM_DPICHANGED): {

                    UINT dpiX = LOWORD(wp);
                    UINT dpiY = HIWORD(wp);
                    float scaleX = dpiX / 96.0f;
                    float scaleY = dpiY / 96.0f;

                    // SetWindowPos synchronously sends WM_SIZE. Its handlers
                    // must apply client minimums and logical layout at the new
                    // DPI, especially when moving from a 2x to a 1x display.
                    self->scale = scaleX;

                    RECT* const suggestedRect = reinterpret_cast<RECT*>(lp);

                    SetWindowPos(
                        h, nullptr,
                        suggestedRect->left, suggestedRect->top,
                        suggestedRect->right - suggestedRect->left,
                        suggestedRect->bottom - suggestedRect->top,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    );

                    self->notifyEvent({
                        WinEvent::Type::Scale,
                        0, 0,
                        (int)(scaleX * 100), (int)(scaleY * 100)
                    });

                    return 0;
                }

                // When the window is moved
                case (WM_MOVE): {

                    self->notifyEvent({
                        WinEvent::Type::Move,
                        0, 0,
                        GET_X_LPARAM(lp),
                        GET_Y_LPARAM(lp)
                    });

                    return 0;
                }

                case (WM_ERASEBKGND): {
                    
                    self->notifyEvent({
                        WinEvent::Type::Clear
                    });

                    return 0;
                }

                case (WM_PAINT): {

                    self->dirty = true;

                    PAINTSTRUCT ps;
                    HDC dc = BeginPaint(h, &ps);

                    self->notifyEvent({
                        WinEvent::Type::Paint
                    });

                    EndPaint(h, &ps);

                    self->dirty = false;
                    self->flushPendingInvalidate();

                    return 0;
                }

                // When the mouse moves
                case (WM_MOUSEMOVE): {

                    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                    ClientToScreen(h, &pt);

                    self->notifyEvent({
                        WinEvent::Type::MouseMove,
                        0, 0,
                        pt.x, pt.y
                    });

                    return 0;
                }

                // Vertical wheel (mouse or touchpad)
                case WM_MOUSEWHEEL: {

                    self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, 0, GET_WHEEL_DELTA_WPARAM(wp)
                    });

                    return 0;
                }

                // Horizontal wheel (mouse tilt or touchpad)
                case WM_MOUSEHWHEEL: {

                    self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, GET_WHEEL_DELTA_WPARAM(wp), 0
                    });

                    return 0;
                }

                // Vertical wheel (trackpad specifically)
                case (WM_POINTERWHEEL): {

                    self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, 0, GET_WHEEL_DELTA_WPARAM(wp)
                    });

                    return 0;
                }

                // Horizontal wheel (trackpad specifically)
                case (WM_POINTERHWHEEL): {

                    self->notifyEvent({ WinEvent::Type::MouseMove, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
                    self->notifyEvent({
                        WinEvent::Type::MouseWheel,
                        0, 0, GET_WHEEL_DELTA_WPARAM(wp), 0
                    });

                    return 0;
                }

                // Mouse up
                case (WM_LBUTTONUP): { self->onButtonUp(h, 0, lp); return 0; }
                case (WM_RBUTTONUP): { self->onButtonUp(h, 1, lp); return 0; }
                case (WM_MBUTTONUP): { self->onButtonUp(h, 2, lp); return 0; }

                // Mouse down
                case (WM_LBUTTONDOWN): { self->onButtonDown(h, 0, lp); return 0; }
                case (WM_RBUTTONDOWN): { self->onButtonDown(h, 1, lp); return 0; }
                case (WM_MBUTTONDOWN): { self->onButtonDown(h, 2, lp); return 0; }

                // Win32 double-click: treat as a normal press so Rev detects it via
                // Event::Button::isDoubleClick() (not ButtonAction::DoubleClick).
                case (WM_LBUTTONDBLCLK): { self->onButtonDown(h, 0, lp); return 0; }
                case (WM_RBUTTONDBLCLK): { self->onButtonDown(h, 1, lp); return 0; }
                case (WM_MBUTTONDBLCLK): { self->onButtonDown(h, 2, lp); return 0; }

                // Capture lost involuntarily (alt-tab, a competing SetCapture)
                // while a button is held — synthesize a release so Rev's press /
                // drag target flags don't get stuck. Our own ReleaseCapture also
                // routes here but only after buttonsDown has reached 0.
                case (WM_CAPTURECHANGED): {

                    if (self && self->buttonsDown > 0) {
                        self->buttonsDown = 0;
                        self->notifyEvent({ WinEvent::Type::CaptureLost });
                    }

                    return 0;
                }
      
                // Keyboard (WM_SYSKEY* is required for Alt / Alt+key on Windows)
                case (WM_KEYDOWN):
                case (WM_SYSKEYDOWN): {
                    if (self && self->notifyKeyboard(wp, 1)) { return 0; }
                    break;
                }

                case (WM_KEYUP):
                case (WM_SYSKEYUP): {
                    if (self && self->notifyKeyboard(wp, 0)) { return 0; }
                    break;
                }

                case (WM_CHAR): { self->notifyEvent({ WinEvent::Type::Character, (uint64_t)(wp) }); return 0; }
            }

            // Must call default window proc first
            return DefWindowProcW(h, msg, wp, lp);
        }

        // Getting OpenGL context
        //--------------------------------------------------

        // Add to your struct:
        HDC   hdc   = nullptr;
        HGLRC hglrc = nullptr;

        // Local WGL typedefs (no loader needed)
        typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
        typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
        typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);

        static ATOM registerDummyClass(HINSTANCE inst, LPCWSTR name, WNDPROC wndproc) {

            WNDCLASSW wc = {
                .style = CS_OWNDC,
                .lpfnWndProc = wndproc,
                .hInstance = inst,
                .lpszClassName = name
            };

            return RegisterClassW(&wc);
        }

        static LRESULT CALLBACK DummyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
            return DefWindowProcW(h, m, w, l);
        }

        void loadGlFunctions() {

            glewExperimental = GL_TRUE;
            GLenum glewStatus = glewInit();

            if (glewStatus != GLEW_OK) {
                const GLubyte* errorStr = glewGetErrorString(glewStatus);
                throw std::runtime_error(
                    std::string("[Canvas] Glew init failed: ")
                    + reinterpret_cast<const char*>(errorStr)
                );
            }

            if (!GLEW_VERSION_4_3 || !(GLEW_VERSION_4_4 || GLEW_ARB_buffer_storage)) {
                throw std::runtime_error("[NativeWindow] Rev requires OpenGL 4.4, or OpenGL 4.3 with GL_ARB_buffer_storage (including software drivers)");
            }

            dbg("");
            dbg("OpenGL INFO");
            dbg("--------------------\n");
            dbg("GLEW version: %s", glewGetString(GLEW_VERSION));
            dbg("OpenGL version: %s", glGetString(GL_VERSION));
            dbg("GLSL version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
            dbg("Renderer: %s", glGetString(GL_RENDERER));
            dbg("Vendor: %s", glGetString(GL_VENDOR));
            dbg("");
        }

        void createContext() {

            if (hglrc) {
                return;
            }

            if (!handle) { throw std::runtime_error("[NativeWindow] No window handle available"); }

            HINSTANCE hinst = GetModuleHandleW(nullptr);

            // 1) DUMMY: class + window + legacy context (to load ARB funcs)
            const wchar_t* dummyClass = L"RevDummyGL";
            static bool dummyRegistered = false;

            if (!dummyRegistered) {

                if (!registerDummyClass(hinst, dummyClass, DummyWndProc))
                   { throw std::runtime_error("[NativeWindow] Failed to register dummy GL class"); }

                dummyRegistered = true;
            }

            HWND dummy = CreateWindowW(dummyClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
            if (!dummy) throw std::runtime_error("[NativeWindow] Failed to create dummy window");

            HDC dummyDC = GetDC(dummy);

            // Basic legacy PFD (good enough to create a legacy context)
            PIXELFORMATDESCRIPTOR pfd = {
                sizeof(PIXELFORMATDESCRIPTOR), 1,
                (WORD)(PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER),
                PFD_TYPE_RGBA, 32,
                0,0,0,0,0,0,
                0, 0, 0, 0,0,0,0,
                24, 8, 0, PFD_MAIN_PLANE, 0, 0,0,0
            };

            // Choose pixel format
            int pf = ChoosePixelFormat(dummyDC, &pfd);
            if (!pf || !SetPixelFormat(dummyDC, pf, &pfd)) {

                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Dummy SetPixelFormat failed");
            }

            // Create dummy context
            HGLRC dummyRC = wglCreateContext(dummyDC);
            if (!dummyRC) {
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglCreateContext failed");
            }

            // Make dummy context current
            if (!wglMakeCurrent(dummyDC, dummyRC)) {
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                throw std::runtime_error("[NativeWindow] Dummy wglMakeCurrent failed");
            }

            // 2) Load ARB entry points
            auto wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
            auto wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
            auto wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

            // Extensions not available – leave the dummy current so GLEW can still load,
            // OR fall back to legacy for the real window (but you'll stay on 2.1).
            // Here we error out to be explicit.
            if (!wglCreateContextAttribsARB || !wglChoosePixelFormatARB) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Required WGL ARB extensions not available");
            }

            // 3) Pick modern pixel format for the REAL window using wglChoosePixelFormatARB
            hdc = GetDC(handle);
            if (!hdc) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] GetDC failed for real window");
            }

            int pixAttribs[] = {
                0x2001 /*WGL_DRAW_TO_WINDOW_ARB*/, TRUE,
                0x2010 /*WGL_SUPPORT_OPENGL_ARB*/, TRUE,
                0x2011 /*WGL_DOUBLE_BUFFER_ARB*/, TRUE,
                0x201C /*WGL_SWAP_METHOD_ARB*/,     0x2028,   // WGL_SWAP_EXCHANGE_ARB — swap buffers efficiently
    
                0x2013 /*WGL_PIXEL_TYPE_ARB*/,     0x202B /*WGL_TYPE_RGBA_ARB*/,
                0x2014 /*WGL_COLOR_BITS_ARB*/,     24,
                0x2022 /*WGL_DEPTH_BITS_ARB*/,     24,
                0x2023 /*WGL_STENCIL_BITS_ARB*/,   8,
                0, 0
            };

            int fmt = 0; UINT fmtCount = 0;
            if (!wglChoosePixelFormatARB(hdc, pixAttribs, nullptr, 1, &fmt, &fmtCount) || fmtCount == 0) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] wglChoosePixelFormatARB failed");
            }

            // You must still call SetPixelFormat once on the real DC (with a legacy PFD; the ARB choice wins)
            PIXELFORMATDESCRIPTOR chosenPFD;
            if (!DescribePixelFormat(hdc, fmt, sizeof(chosenPFD), &chosenPFD) || !SetPixelFormat(hdc, fmt, &chosenPFD)) {
                
                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);
                
                throw std::runtime_error("[NativeWindow] SetPixelFormat (real) failed");
            }

            // 4) Create a modern Core profile context (try 4.6, fall back to 4.5, 4.4, ... 3.3)
            const int versions[][2] = { {4,6},{4,5},{4,4},{4,3},{4,2},{4,1},{4,0},{3,3} };
            HGLRC realRC = nullptr;

            for (auto& v : versions) {

                int ctxAttribs[] = {
                    0x2091 /*WGL_CONTEXT_MAJOR_VERSION_ARB*/, v[0],
                    0x2092 /*WGL_CONTEXT_MINOR_VERSION_ARB*/, v[1],
                    0x9126 /*WGL_CONTEXT_PROFILE_MASK_ARB*/,  0x00000001 /*WGL_CONTEXT_CORE_PROFILE_BIT_ARB*/,
                    // Optional debug:
                    // 0x2094 /*WGL_CONTEXT_FLAGS_ARB*/, 0x0001 /*WGL_CONTEXT_DEBUG_BIT_ARB*/,
                    0
                };

                HGLRC share = nullptr;

                // Explicitly establish the root context
                if (sharedRoot) {
                    share = sharedRoot;
                }

                realRC = wglCreateContextAttribsARB(hdc, share, ctxAttribs);

                if (realRC && !sharedRoot) {
                    sharedRoot = realRC;
                    resourceRoot = this;
                }

                if (realRC) { break; }
            }

            if (!realRC) {

                wglMakeCurrent(nullptr, nullptr);
                wglDeleteContext(dummyRC);
                ReleaseDC(dummy, dummyDC);
                DestroyWindow(dummy);

                throw std::runtime_error("[NativeWindow] Failed to create modern GL context");
            }

            // 5) Activate the real context
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(dummyRC);
            ReleaseDC(dummy, dummyDC);
            DestroyWindow(dummy);

            if (!wglMakeCurrent(hdc, realRC)) {
                wglDeleteContext(realRC);
                ReleaseDC(handle, hdc); hdc = nullptr;
                throw std::runtime_error("[NativeWindow] wglMakeCurrent (real) failed");
            }

            hglrc = realRC;

            // Optional: enable vsync if extension is present
            if (wglSwapIntervalEXT) {
                wglSwapIntervalEXT(0); // 1 = vsync on, 0 = off
            }
        }

        void makeContextCurrent() {

            if (!hdc || !hglrc) {
                throw std::runtime_error("[NativeWindow] Invalid GL context");
            }

            if (!wglMakeCurrent(hdc, hglrc)) {
                throw std::runtime_error("[NativeWindow] wglMakeCurrent failed");
            }

            currentWindow = this;
        }

        bool tryMakeContextCurrent() {

            if (!hglrc || !handle) {
                return false;
            }

            if (!hdc) {
                hdc = GetDC(handle);
            }

            if (!hdc) {
                return false;
            }

            if (wglMakeCurrent(hdc, hglrc)) {
                currentWindow = this;
                return true;
            }

            // The cached DC can go stale; re-acquire once and retry.
            ReleaseDC(handle, hdc);
            hdc = GetDC(handle);

            if (!hdc) {
                return false;
            }

            if (!wglMakeCurrent(hdc, hglrc)) {
                return false;
            }

            currentWindow = this;
            return true;
        }

        bool isContextCurrent() const {

            return (
                hdc &&
                hglrc &&
                wglGetCurrentDC() == hdc &&
                wglGetCurrentContext() == hglrc
            );
        }
        
        void swapBuffers() {
            if (hdc) {
                this->dirty = false;
                SwapBuffers(hdc);
            }
        }

        void getFramebufferSize(int& fbW, int& fbH) const {
            if (!handle) {
                fbW = fbH = 0;
                return;
            }
        
            RECT rect;
            GetClientRect(handle, &rect);
        
            UINT dpi = GetDpiForWindow(handle);
            float scale = dpi / 96.0f;
        
            fbW = static_cast<int>(std::round((rect.right - rect.left) * scale));
            fbH = static_cast<int>(std::round((rect.bottom - rect.top) * scale));
        }
    };
};
