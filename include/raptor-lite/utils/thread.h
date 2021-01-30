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

#ifndef __RAPTOR_LITE_UTILS_THREAD__
#define __RAPTOR_LITE_UTILS_THREAD__

#include <stddef.h>
#include <functional>

namespace raptor {

class ThreadInterface {
public:
    virtual ~ThreadInterface() {}
    virtual void Start() = 0;
    virtual void Join() = 0;
};

class Thread {
public:
    using Callback = std::function<void(void *)>;

    class Options {
    public:
        Options()
            : _joinable(true)
            , _stack_size(0) {}

        Options &SetJoinable(bool joinable) {
            _joinable = joinable;
            return *this;
        }

        bool Joinable() const {
            return _joinable;
        }

        Options &SetStackSize(size_t size) {
            _stack_size = size;
            return *this;
        }

        size_t StackSize() const {
            return _stack_size;
        }

    private:
        bool _joinable;
        size_t _stack_size;
    };  // class Options

    Thread();
    Thread(const char *thread_name, Thread::Callback cb, void *arg, bool *success = nullptr,
           const Options &options = Options());

    ~Thread() = default;

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    // movable construction
    Thread(Thread &&oth);
    Thread &operator=(Thread &&oth);

    void Start();
    void Join();

private:
    enum State {
        kNull,
        kAlive,
        kActive,
        kFinish,
        kFailed,
    };

    ThreadInterface *_impl;
    State _state;
};

}  // namespace raptor

#endif  // __RAPTOR_LITE_UTILS_THREAD__
