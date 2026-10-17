#pragma once
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VoxelCraft", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VoxelCraft", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VoxelCraft", __VA_ARGS__)
#else
#include <cstdio>
#define VC_LOG_(stream, tag, ...)                \
    do {                                         \
        std::fprintf(stream, "[%s] ", tag);      \
        std::fprintf(stream, __VA_ARGS__);       \
        std::fputc('\n', stream);                \
        std::fflush(stream);                     \
    } while (0)
#define LOGI(...) VC_LOG_(stdout, "I", __VA_ARGS__)
#define LOGW(...) VC_LOG_(stderr, "W", __VA_ARGS__)
#define LOGE(...) VC_LOG_(stderr, "E", __VA_ARGS__)
#endif
