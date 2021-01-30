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

#ifndef __RAPTOR_LITE_UTILS_LOG__
#define __RAPTOR_LITE_UTILS_LOG__

#include <stdlib.h>
#include "raptor-lite/utils/useful.h"

namespace raptor {
enum class LogLevel : int { kDebug, kInfo, kError, kDisable };

typedef struct {
    const char *file;
    int line;
    LogLevel level;
    const char *message;
} LogArgument;

typedef void (*LogPrintCallback)(LogArgument *arg);

void LogSetLevel(LogLevel level);
void LogFormatPrint(const char *file, int line, LogLevel level, const char *format, ...);
void LogSetPrintCallback(LogPrintCallback callback);

}  // namespace raptor

#define log_debug(FMT, ...)                                                                        \
    raptor::LogFormatPrint(__FILE__, __LINE__, raptor::LogLevel::kDebug, FMT, ##__VA_ARGS__)

#define log_info(FMT, ...)                                                                         \
    raptor::LogFormatPrint(__FILE__, __LINE__, raptor::LogLevel::kInfo, FMT, ##__VA_ARGS__)

#define log_error(FMT, ...)                                                                        \
    raptor::LogFormatPrint(__FILE__, __LINE__, raptor::LogLevel::kError, FMT, ##__VA_ARGS__)

#define RAPTOR_ASSERT(x)                                                                           \
    do {                                                                                           \
        if (RAPTOR_UNLIKELY(!(x))) {                                                               \
            log_error("assertion failed: %s", #x);                                                 \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

#endif  // __RAPTOR_LITE_UTILS_LOG__
