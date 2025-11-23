/**
 * @file platform.h
 * @brief 跨平台编译配置头文件，自动识别操作系统并定义平台宏
 * @details 
 *  该文件通过预编译指令检测当前编译环境的操作系统，自动定义对应的平台宏，
 *  便于后续代码中使用条件编译实现跨平台兼容逻辑。
 *  支持的平台包括：Linux、macOS、FreeBSD（可扩展添加其他平台）。
 * @note 
 *  1. 平台检测优先级：先检测Linux，再检测macOS，最后检测FreeBSD；\n
 *  2. 每个平台宏定义为1，未匹配到任何平台时不定义任何宏；\n
 *  3. 可通过添加新的预编译分支扩展支持其他操作系统（如Windows、Android等）；\n
 *  4. 使用示例：\code{.c}
 *     #if defined(OS_LINUX)
 *         // Linux-specific code
 *     #elif defined(OS_MACOSX)
 *         // macOS-specific code
 *     #endif
 *     \endcode
 */

#ifndef PLATFORM_H
#define PLATFORM_H

/* ------------------------------------------------------------------------- */
/** 
 * @def OS_LINUX
 * @brief 标识当前平台为 Linux。
 * 
 * 当检测到 `__linux__`、`linux` 或 `LINUX` 预定义宏时定义此宏。
 */
#if defined(__linux__) || defined(linux) || defined(LINUX)
    #ifndef OS_LINUX
        #define OS_LINUX 1
    #endif
#endif

/* ------------------------------------------------------------------------- */
/** 
 * @def OS_MACOSX
 * @brief 标识当前平台为 macOS。
 * 
 * 当同时定义 `__APPLE__` 和 `__MACH__` 时（这是 Apple Darwin 内核的标准组合），定义此宏。
 */
#if defined(__APPLE__) && defined(__MACH__)
    #ifndef OS_MACOSX
        #define OS_MACOSX 1
    #endif
#endif

/* ------------------------------------------------------------------------- */
/** 
 * @def OS_FREEBSD
 * @brief 标识当前平台为 FreeBSD。
 * 
 * 当检测到 `__FreeBSD__` 预定义宏时定义此宏。
 */
#if defined(__FreeBSD__)
    #ifndef OS_FREEBSD
        #define OS_FREEBSD 1
    #endif
#endif

/* ------------------------------------------------------------------------- */
/**
 * @note 若以上平台均未匹配，则不会定义任何平台宏（如 Windows、Solaris 等需手动扩展）。
 */

#endif /* PLATFORM_H */