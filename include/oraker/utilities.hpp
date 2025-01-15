#pragma once

// use #pragma GCC as MSVC is unsupported due to macOS-specific code
// clang supports #pragma GCC

#if defined(__GNUC__) || defined(__clang__)

#define ORAKER_PRAGMA(pragma) _Pragma(#pragma)
#define ORAKER_DIAGNOSTIC_PUSH ORAKER_PRAGMA(GCC diagnostic push)
#define ORAKER_DIAGNOSTIC_POP ORAKER_PRAGMA(GCC diagnostic pop)
#define ORAKER_IGNORE_DIAGNOSTIC(option) ORAKER_PRAGMA(GCC diagnostic ignored #option)

#define ORAKER_IGNORE_DEPRECATION_PUSH \
    ORAKER_DIAGNOSTIC_PUSH            \
    ORAKER_IGNORE_DIAGNOSTIC(-Wdeprecated-declarations)

#else
#error "unsupported compiler for warning disablement
#endif
