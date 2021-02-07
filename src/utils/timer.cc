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

#include "raptor-lite/utils/timer.h"

#include <assert.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include <functional>

#include "raptor-lite/utils/time.h"

namespace raptor {
struct TimerNode {
    MultiProducerSingleConsumerQueue::Node node;
    uint64_t tid;
    int64_t dead_line;
};

Timer::Timer(TimerHandler *p)
    : _handler(p)
    , _shutdown(true) {}

Timer::~Timer() {}

void Timer::Init() {
    if (!_shutdown) return;
    _shutdown = false;
    bool success = false;
    _thd = Thread("timer", std::bind(&Timer::WorkThread, this, std::placeholders::_1), nullptr,
                  &success);
    assert(success);
}

void Timer::Start() {
    _thd.Start();
}

void Timer::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;
        _thd.Join();

        // clear message queue
        bool empty = true;
        do {
            auto n = _mq.PopAndCheckEnd(&empty);
            auto timer = reinterpret_cast<TimerNode *>(n);
            if (timer != nullptr) {
                delete timer;
            }
        } while (!empty);
    }
}

void Timer::SetTimer(uint32_t tid1, uint32_t tid2, uint32_t delay_ms) {
    auto tn = new TimerNode;
    tn->dead_line = GetCurrentMilliseconds() + delay_ms;
    tn->tid = uint64_t(tid1) << 32 | tid2;
    _mq.push(&tn->node);
}

void Timer::WorkThread(void *) {
    while (!_shutdown) {

        auto n = _mq.pop();
        auto timer = reinterpret_cast<struct TimerNode *>(n);

        if (timer != nullptr) {
            _tbls.insert({timer->dead_line, timer->tid});
            delete timer;
        }

        int64_t begin_millseconds = GetCurrentMilliseconds();
        int64_t current_millseconds = 0;

        int timer_count = 0;

        auto it = _tbls.begin();
        while (it != _tbls.end()) {
            if (it->first > begin_millseconds) {
                break;
            }

            uint64_t tid = it->second;

            it = _tbls.erase(it);

            _handler->OnTimer(static_cast<uint32_t>(tid >> 32),
                              static_cast<uint32_t>(tid & 0xffffffff));

            timer_count++;

            current_millseconds = GetCurrentMilliseconds();

            if (timer_count >= 3 || current_millseconds - begin_millseconds > 300) {
                break;
            }
        }

        if (timer_count < 3) {
            int interval = static_cast<int>(current_millseconds - begin_millseconds);
            if (interval < 300) {
#ifdef _WIN32
                Sleep(300 - interval);
#else
                usleep((300 - interval) * 1000);
#endif
            }
        }
    }
}
}  // namespace raptor
