#ifndef DBG_H
#define DBG_H

#ifdef DEBUG
#include <cstdio>  // for printf, vprintf
#include <cstdarg> // for va_list, va_start, va_end

// Simple printf wrapper with immediate flushing for debug builds
inline void dbg(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}
#else
// In production builds, dbg does nothing
inline void dbg(const char*, ...) {}
#endif // DEBUG

#endif // DBG_H
