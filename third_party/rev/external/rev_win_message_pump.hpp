#pragma once
#include <windows.h>

namespace RevWinMessagePump {
// Yield to the application even if callbacks keep posting more input. A paint
// can queue its successor, so render at most one frame before the next poll.
inline constexpr unsigned message_limit = 64;
inline bool pump() {
    MSG message{};
    for (unsigned processed = 0; processed < message_limit; ++processed) {
        // Include message-only service windows on this thread, not just the
        // main HWND. Never wait here: receiver progress belongs to the caller.
        if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) return true;
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (message.message == WM_PAINT) return true;
    }
    return true;
}
}
