// internal/platform.h 
#pragma once

#ifdef _WIN32
    #define PMAKE_OS_WINDOWS 1
    #define PMAKE_PATH_SEP '\\'
#elif __linux__
    #define PMAKE_OS_LINUX 1
    #define PMAKE_PATH_SEP '/'
#elif __APPLE__
    #define PMAKE_OS_MAC 1
    #define PMAKE_PATH_SEP '/'
#else
    #error "Unsupported platform"
#endif

// Функции для работы с путями, если нужны
namespace pmake::platform {

inline char pathSeparator() { return PMAKE_PATH_SEP; }

} // namespace pmake::platform
