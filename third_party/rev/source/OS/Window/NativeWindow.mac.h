#pragma once

#include "WinEvent.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RevMacWindowHandle;
typedef void(*RevMacEventAcceptor)(void* userData, WinEvent ev);


RevMacWindowHandle rev_mac_window_create(
    int width, int height,
    int minWidth, int minHeight,
    int maxWidth, int maxHeight,
    bool borderless,
    void* user, RevMacEventAcceptor acceptor, void* parent
);
void rev_mac_window_destroy(RevMacWindowHandle handle);
void rev_mac_window_set_size(RevMacWindowHandle handle, int w, int h);
void rev_mac_window_request_frame(RevMacWindowHandle handle);
void rev_mac_wait_event();

#ifdef __cplusplus
}
#endif
