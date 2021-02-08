/*
 *
 * Copyright (c) 2020 The Raptor Authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "raptor-lite/utils/log.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <Windows.h>
#include <processthreadsapi.h>
#else
#include <pthread.h>
#endif

#include "raptor-lite/utils/atomic.h"
#include "raptor-lite/utils/color.h"
#include "raptor-lite/utils/time.h"

namespace raptor {
namespace {

void log_default_print(LogArgument *args);

AtomicIntptr g_log_function((intptr_t)log_default_print);
AtomicIntptr g_min_level((intptr_t)LogLevel::kDebug);
char g_level_char[static_cast<int>(LogLevel::kDisable)] = {'D', 'I', 'W', 'E'};
Color g_fc_table[static_cast<int>(LogLevel::kDisable)] = {Cyan, Green, Yellow, Red};
Color g_bc_table[static_cast<int>(LogLevel::kDisable)] = {Black, Black, Black, White};

#ifdef _WIN32
static __declspec(thread) unsigned long tls_tid = 0;
constexpr char delimiter = '\\';
#else
static __thread unsigned long tls_tid = 0;
constexpr char delimiter = '/';
#endif

void log_default_print(LogArgument *args) {
    if (tls_tid == 0) {
#ifdef _WIN32
        tls_tid = static_cast<unsigned long>(GetCurrentThreadId());
#else
        tls_tid = static_cast<unsigned long>(pthread_self());
#endif
    }

    const char *last_slash = NULL;
    const char *display_file = NULL;
    char time_buffer[64] = {0};

    last_slash = strrchr(args->file, delimiter);
    if (last_slash == NULL) {
        display_file = args->file;
    } else {
        display_file = last_slash + 1;
    }

    raptor_time_spec now;
    GetTimeOfDay(&now);
    time_t timer = now.tv_sec;

#ifdef _WIN32
    struct tm stm;
    if (localtime_s(&stm, &timer)) {
        strcpy(time_buffer, "error:localtime");
    }
#else
    struct tm stm;
    if (!localtime_r(&timer, &stm)) {
        strcpy(time_buffer, "error:localtime");
    }
#endif
    // "%F %T" 2020-05-10 01:43:06
    else if (0 == strftime(time_buffer, sizeof(time_buffer), "%F %T", &stm)) {
        strcpy(time_buffer, "error:strftime");
    }

    // choose color
    Color fc = g_fc_table[static_cast<int>(args->level)];
    Color bc = g_bc_table[static_cast<int>(args->level)];

    SetConsoleColor(stderr, fc, bc);

    fprintf(stderr, "[%s.%06d %7lu %c] %s (%s:%d)", time_buffer,
            now.tv_usec,  // microseconds
            tls_tid, g_level_char[static_cast<int>(args->level)], args->message, display_file,
            args->line);

    ResetConsoleColor(stderr);

    fprintf(stderr, "\n");

    fflush(stderr);
}
}  // namespace

// ---------------------------

void LogSetLevel(LogLevel level) {
    g_min_level.Store((intptr_t)level);
}

void LogSetPrintCallback(LogPrintCallback func) {
    g_log_function.Store((intptr_t)func);
}

void LogFormatPrint(const char *file, int line, LogLevel level, const char *format, ...) {

    char *message = NULL;
    va_list args;
    va_start(args, format);

#ifdef _WIN32
    int ret = _vscprintf(format, args);
    va_end(args);
    if (ret < 0) {
        return;
    }

    size_t buff_len = (size_t)ret + 1;
    message = (char *)malloc(buff_len);
    va_start(args, format);
    ret = vsnprintf_s(message, buff_len, _TRUNCATE, format, args);
    va_end(args);
#else
    if (vasprintf(&message, format, args) == -1) {  // stdio.h
        va_end(args);
        return;
    }
#endif

    if (g_min_level.Load() <= static_cast<intptr_t>(level)) {
        LogArgument tmp;
        tmp.file = file;
        tmp.line = line;
        tmp.level = level;
        tmp.message = message;
        ((LogPrintCallback)g_log_function.Load())(&tmp);
    }
    free(message);
}
}  // namespace raptor
