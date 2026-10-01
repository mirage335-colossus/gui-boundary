module;

#include <vector>
#include <algorithm>
#include <cstdint>
#include <windows.h>

export module Rev.Application;

import Rev.Window;
import Rev.Core.Process;

export namespace Rev {

    struct Application {

        std::vector<void*> windows;

        Application() {}

        ~Application() {}

        void pumpProcess() {
            Rev::Core::Process::instance().tick();
        }

        // Run every due tick. Style transitions can queue several WM_PAINT
        // messages per loop iteration; ticking only once after the batch stalls
        // Process-driven animation until the transition chain ends.
        void pumpProcessDue() {

            auto& process = Rev::Core::Process::instance();

            while (process.hasScheduledTicks() && process.msUntilNextTick() == 0) {
                pumpProcess();
            }
        }

        void run() {

            MSG msg = { 0 };

            while (!windows.empty()) {

                DWORD timeout = INFINITE;

                if (Rev::Core::Process::instance().hasScheduledTicks()) {

                    const uint64_t waitMs = Rev::Core::Process::instance().msUntilNextTick();
                    const uint64_t clamped = waitMs > 0 ? waitMs : 1;

                    timeout = static_cast<DWORD>(clamped);
                }

                MsgWaitForMultipleObjects(
                    0,
                    nullptr,
                    FALSE,
                    timeout,
                    QS_ALLINPUT
                );

                pumpProcessDue();

                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

                    if (msg.message == WM_QUIT) {
                        for (void* handle : windows) {
                            delete static_cast<Window*>(handle);
                        }
                        windows.clear();
                        return;
                    }

                    TranslateMessage(&msg);
                    DispatchMessage(&msg);

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

                pumpProcessDue();
            }
        }

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
