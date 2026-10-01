#pragma once
#include <cstdint>

struct WinEvent {

    enum Type {
        Null,
        Create, Destroy, Close,
        Focus, Defocus,
        Move, Resize, Maximize, Minimize, Restore,
        Scale, Clear, Paint,
        MouseButton, MouseMove, MouseWheel, CaptureLost,
        Keyboard, Character
    };

    Type type;
    uint64_t a, b;
    // MouseMove / MouseButton: c,d are absolute screen coordinates in
    // physical pixels. Window converts them once to client logical units.
    // MouseWheel: c,d are deltas, 120 units per wheel notch. Emit MouseMove at
    // the wheel event's screen position first so hit testing does not depend
    // on an earlier pointer move.
    int64_t c, d;

    bool rejected = false;
    void* subject = nullptr;

    void reject() {
        this->rejected = true;
    }
};
