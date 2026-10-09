#pragma once

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstring>

// 日志等级
#define LOG_TRACE 0
#define LOG_DEBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4
#define LOG_FATAL 5

// 默认日志等级
#ifndef DEFAULT_LOG_LEVEL
#define DEFAULT_LOG_LEVEL LOG_DEBUG
#endif

// ANSI 颜色
#define COLOR_RESET "\033[0m"

#define COLOR_GRAY "\033[90m"
#define COLOR_CYAN "\033[36m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED "\033[31m"
#define COLOR_MAGENTA "\033[35m"

// 根据等级获取名字
inline const char *LogLevelName(int level)
{
    switch (level)
    {
    case LOG_TRACE:
        return "TRACE";
    case LOG_DEBUG:
        return "DEBUG";
    case LOG_INFO:
        return "INFO ";
    case LOG_WARN:
        return "WARN ";
    case LOG_ERROR:
        return "ERROR";
    case LOG_FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

// 根据等级获取颜色
inline const char *LogLevelColor(int level)
{
    switch (level)
    {
    case LOG_TRACE:
        return COLOR_GRAY;
    case LOG_DEBUG:
        return COLOR_CYAN;
    case LOG_INFO:
        return COLOR_GREEN;
    case LOG_WARN:
        return COLOR_YELLOW;
    case LOG_ERROR:
        return COLOR_RED;
    case LOG_FATAL:
        return COLOR_MAGENTA;
    default:
        return COLOR_RESET;
    }
}

// 去除完整路径，只保留文件名

inline const char *LogFileName(const char *path)
{
    const char *p = std::strrchr(path, '/');

    return p ? p + 1 : path;
}

// 真正打印日志
inline void LogPrint(
    int level,
    const char *file,
    int line,
    const char *func,
    const char *format,
    ...)
{
    if (level < DEFAULT_LOG_LEVEL)
        return;

    // 防止多个线程日志交叉
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);

    // 当前时间
    auto now = std::chrono::system_clock::now();

    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;

    std::time_t t =
        std::chrono::system_clock::to_time_t(now);

    struct tm tm_time{};

#if defined(_WIN32)
    localtime_s(&tm_time, &t);
#else
    localtime_r(&t, &tm_time);
#endif

    char time_buf[32];

    std::strftime(
        time_buf,
        sizeof(time_buf),
        "%H:%M:%S",
        &tm_time);

    // 线程ID
    size_t tid =
        std::hash<std::thread::id>{}(
            std::this_thread::get_id());

    const char *color =
        LogLevelColor(level);

    // 前缀
    std::fprintf(
        stdout,
        "%s[%s] [%s.%03lld] [T:%zu] [%s:%d %s] ",
        color,
        LogLevelName(level),
        time_buf,
        static_cast<long long>(ms.count()),
        tid,
        LogFileName(file),
        line,
        func);

    // 用户真正传入的日志
    va_list args;
    va_start(args, format);

    std::vfprintf(
        stdout,
        format,
        args);

    va_end(args);

    // 恢复颜色
    std::fprintf(
        stdout,
        "%s\n",
        COLOR_RESET);

    std::fflush(stdout);
}

// 日志宏
#define TRACE_LOG(format, ...) \
    LogPrint(LOG_TRACE, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define DBG_LOG(format, ...) \
    LogPrint(LOG_DEBUG, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define INF_LOG(format, ...) \
    LogPrint(LOG_INFO, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define WARN_LOG(format, ...) \
    LogPrint(LOG_WARN, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define ERR_LOG(format, ...) \
    LogPrint(LOG_ERROR, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

#define FATAL_LOG(format, ...) \
    LogPrint(LOG_FATAL, __FILE__, __LINE__, __func__, format, ##__VA_ARGS__)
