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

#ifndef __RAPTOR_UTILS_TIMER__
#define __RAPTOR_UTILS_TIMER__

#include <stdint.h>
#include <map>

#include "raptor-lite/utils/mpscq.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/thread.h"

namespace raptor {
class TimerHandler {
public:
    virtual ~TimerHandler() {}
    virtual void OnTimer(uint32_t tid1, uint32_t tid2) = 0;
};

class Timer final {
public:
    explicit Timer(TimerHandler *p);
    ~Timer();

    raptor_error Init();
    void Start();
    void Shutdown();

    void SetTimer(uint32_t tid1, uint32_t tid2, uint32_t delay_ms);

private:
    void WorkThread(void *);

    TimerHandler *_handler;
    bool _shutdown;
    std::multimap<int64_t, uint64_t> _tbls;
    MultiProducerSingleConsumerQueue _mq;
    Thread _thd;
};

}  // namespace raptor

#endif  // __RAPTOR_UTILS_TIMER__
