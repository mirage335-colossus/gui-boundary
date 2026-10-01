module;

#include <functional>
#include <unordered_map>
#include <dbg.hpp>
#include "WinEvent.hpp"
#include "NativeWindow.mac.h"

export module Rev.NativeWindow;

import Rev.Appearance;

export namespace Rev {

    struct NativeWindow {

        enum ButtonAction { Release, Press, DoubleClick };
        enum MouseButton { Left, Right, Middle };

        enum class Key {
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

        const char* keyToString(int key) { return keyToString(Key(key)); }
        const char* keyToString(Key key) { }

        struct Size { int w, h, minW, minH, maxW, maxH; };

        RevMacWindowHandle handle = nullptr;
        using EventCallback = std::function<void(WinEvent&)>;

        // ctor / dtor
        NativeWindow(void* parent,
                     Size size = { 600, 400, 0, 0, 1000, 1000 },
                     bool borderless = false,
                     EventCallback callback = nullptr) {

            this->size = size;
            this->callback = callback;

            handle = rev_mac_window_create(
                size.w, size.h,
                size.minW, size.minH,
                size.maxW, size.maxH,
                borderless,
                this, &this->handleEvent, parent
            );
        };

        ~NativeWindow() {
            rev_mac_window_destroy(handle);
        };

        // methods
        void setSize(int w, int h) {
            rev_mac_window_set_size(handle, w, h);
        };

        void setCursor(Appearance::Cursor newCursor) {

        }

        void requestFrame() {
            if (dirty) {
                invalidatePending = true;
                return;
            }
            dirty = true;
            rev_mac_window_request_frame(handle);
        }

        void flushPendingInvalidate() {
            if (!invalidatePending) { return; }
            invalidatePending = false;
            rev_mac_window_request_frame(handle);
        }

        WinEvent notifyEvent(WinEvent event) {
            if (callback) { callback(event); }
            return event;
        }

        void createContext();
        void makeContextCurrent() {};
        void loadGlFunctions() {};

        void swapBuffers() {
            this->dirty = false;
            flushPendingInvalidate();
        };

        Size size;
        float scale = 1.0f;
        Appearance::Cursor cursor;

        bool dirty = false;
        bool invalidatePending = false;

        EventCallback callback;
        void* windowImpl = nullptr; // Opaque Cocoa types

        // Event handling
        //--------------------------------------------------

        static void handleEvent(void* self, WinEvent ev) {

            NativeWindow* selfWin = static_cast<NativeWindow*>(self);

            switch (ev.type) {

                // Capture resize
                case (WinEvent::Type::Resize): {

                    selfWin->size.w = ev.c;
                    selfWin->size.h = ev.d;

                    break;
                }

                case (WinEvent::Type::MouseButton): {

                    // Flip y axis
                    ev.d = selfWin->size.h - ev.d;

                    break;
                }

                case (WinEvent::Type::MouseMove): {

                    ev.d = selfWin->size.h - ev.d;

                    break;
                }
            }

            selfWin->notifyEvent(ev);
        }
    };
}
