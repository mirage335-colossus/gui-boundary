module;

#include <vector>
#include <algorithm>

#include "../Window/NativeWindow.mac.h"

export module Rev.Application;

import Rev.Window;
import Rev.Core.Process;

export namespace Rev {

    struct Application {

        std::vector<void*> windows;

        // Create
        Application() {
            // Win32 needs an HINSTANCE, but window creation
            // will handle RegisterClass etc. at that layer.
        }

        // Destroy
        ~Application() {
            // Nothing to do here yet, unless we global-cleanup something.
        }

        void pumpProcessDue() {

            auto& process = Rev::Core::Process::instance();

            while (process.hasScheduledTicks() && process.msUntilNextTick() == 0) {
                Rev::Core::Process::instance().tick();
            }
        }

        void run() {

            while (!windows.empty()) {

                rev_mac_wait_event();

                pumpProcessDue();

                for (auto it = windows.begin(); it != windows.end();) {

                    Window* w = static_cast<Window*>(*it);

                    if (w->shouldClose) {
                        it = windows.erase(it);
                        delete w;
                    }

                    else { ++it; }
                }
            }
        }

        // Remove window from our list
        void removeWindow(Window* target) {

            void* handle = static_cast<void*>(target);

            auto it = std::find(windows.begin(), windows.end(), handle);
            
            if (it != windows.end()) {
                it = windows.erase(it);
                delete target;
            }
        }
    };
}
