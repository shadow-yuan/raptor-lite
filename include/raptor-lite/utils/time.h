
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

#ifndef __RAPTOR_LITE_UTILS_TIME__
#define __RAPTOR_LITE_UTILS_TIME__

#include <stdint.h>

typedef struct {
    int64_t tv_sec;  /* seconds */
    int32_t tv_usec; /* and microseconds */
} raptor_time_spec;

// impl of gettimeofday
int32_t GetTimeOfDay(raptor_time_spec *rts);
int64_t GetCurrentMilliseconds();

#endif  // __RAPTOR_LITE_UTILS_TIME__
