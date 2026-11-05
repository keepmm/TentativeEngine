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

// SAL 注釈は MSVC 専用なので、Android では空にする
#if TE_PLATFORM_ANDROID
#define _In_
#define _In_opt_
#define _In_reads_(count)
#define _Out_writes_(count)
#endif
