
// 平台宏定义，自动识别并定义对应操作系统宏
// Linux 平台检测
#if defined(__linux__) || defined(linux) || defined(LINUX)
    #if !defined(OS_LINUX)
        #define OS_LINUX 1
    #endif
#endif

// macOS 平台检测（__APPLE__ 和 __MACH__ 是苹果系统的标准预定义宏）
#if defined(__APPLE__) && defined(__MACH__)
    #if !defined(OS_MACOSX)
        #define OS_MACOSX 1
    #endif
#endif

// 可以继续添加其他平台，如 FreeBSD
#if defined(__FreeBSD__)
    #if !defined(OS_FREEBSD)
        #define OS_FREEBSD 1
    #endif
#endif