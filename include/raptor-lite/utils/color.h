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

#ifndef __RAPTOR_LITE_UTILS_COLOR__
#define __RAPTOR_LITE_UTILS_COLOR__

#include <stdio.h>

namespace raptor {

enum class Color { Default = 0, Black, Red, Green, Yellow, Blue, Magenta, Cyan, White };

// f = stdout, stderror
void SetConsoleColor(FILE *f, Color foreground, Color background);
void ResetConsoleColor(FILE *f = stderr);
void FprintColorTextLine(FILE *f, Color foreground, Color background, const char *text);

}  // namespace raptor

#endif  // __RAPTOR_LITE_UTILS_COLOR__
