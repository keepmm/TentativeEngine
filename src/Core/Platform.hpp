#pragma once

#if defined(__ANDROID__)
#define TE_PLATFORM_ANDROID 1
#define TE_PLATFORM_WINDOWS 0
#elif defined(_WIN32)
#define TE_PLATFORM_ANDROID 0
#define TE_PLATFORM_WINDOWS 1
#else
#error "未対応のプラットフォーム"
#endif