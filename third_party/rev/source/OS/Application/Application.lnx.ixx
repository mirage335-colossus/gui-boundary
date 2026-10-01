module;

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

export module Rev.Application;

import Rev.Window;
import Rev.NativeWindow;

export namespace Rev {

    struct Application {

        std::vector<void*> windows;

        Application() {}
        ~Application() {}

        void run() {
            while (!windows.empty()) {
                NativeWindow::pumpEvents();

                for (auto it = windows.begin(); it != windows.end();) {
                    Window* window = static_cast<Window*>(*it);
                    if (!window) {
                        it = windows.erase(it);
                        continue;
                    }

                    if (window->shouldClose || (window->window && window->window->closed)) {
                        it = windows.erase(it);
                        delete window;
                    }
                    else {
                        ++it;
                    }
                }

                if (windows.empty()) { break; }

                if (NativeWindow::hasPendingEvents()) {
                    continue;
                }

                if (NativeWindow::hasActiveWork()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else {
                    NativeWindow::waitForEvents(16);
                }
            }
        }

        void removeWindow(Window* target) {
            void* handle = static_cast<void*>(target);
            auto it = std::find(windows.begin(), windows.end(), handle);
            if (it != windows.end()) {
                windows.erase(it);
                delete target;
            }
        }
    };
}
