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

#include "src/linux/epoll_thread.h"
#include "raptor-lite/utils/time.h"
#include "src/common/service.h"

namespace raptor {
PollingThread::PollingThread(internal::EventReceivingService *rcv)
    : _receiver(rcv)
    , _shutdown(true)
    , _enable_timeout_check(false)
    , _number_of_threads(0)
    , _running_threads(0) {}

PollingThread::~PollingThread() {
    Shutdown();
}

RefCountedPtr<Status> PollingThread::Init(int threads, int) {
    if (!_shutdown) {
        return RAPTOR_ERROR_NONE;
    }

    raptor_error err = _epoll.create();
    if (err != RAPTOR_ERROR_NONE) {
        return err;
    }

    _shutdown = false;
    _number_of_threads = threads;
    _threads = new Thread[threads];

    for (int i = 0; i < threads; i++) {
        bool success = false;

        _threads[i] =
            Thread("epoll:thread", std::bind(&PollingThread::DoWork, this, std::placeholders::_1),
                   nullptr, &success);

        if (!success) {
            break;
        }

        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("PollingThread failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error PollingThread::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("PollingThread is not initialized");
    }

    for (int i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void PollingThread::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        for (int i = 0; i < _running_threads; i++) {
            _threads[i].Join();
        }
        delete[] _threads;
        _epoll.shutdown();
    }
}

void PollingThread::EnableTimeoutCheck(bool b) {
    _enable_timeout_check = b;
}

void PollingThread::DoWork(void *ptr) {
    while (!_shutdown) {

        if (_enable_timeout_check) {
            int64_t current_time = GetCurrentMilliseconds();
            _receiver->OnTimeoutCheck(current_time);
        }

        int number_of_fd = _epoll.polling();
        if (_shutdown) {
            return;
        }
        if (number_of_fd <= 0) {
            continue;
        }

        for (int i = 0; i < number_of_fd; i++) {
            struct epoll_event *ev = _epoll.get_event(i);

            EventDetail detail;
            detail.ptr = ev->data.ptr;
            detail.error_code = 0;
            detail.event_type = 0;

            if (ev->events & EPOLLERR || ev->events & EPOLLHUP || ev->events & EPOLLRDHUP) {
                detail.event_type |= internal::kErrorEvent;
                detail.error_code = errno;
                _receiver->OnEventProcess(&detail);
                continue;
            }
            if (ev->events & EPOLLIN) {
                detail.event_type |= internal::kRecvEvent;
            }
            if (ev->events & EPOLLOUT) {
                detail.event_type |= internal::kSendEvent;
            }
            _receiver->OnEventProcess(&detail);
        }
    }
}

int PollingThread::Add(int fd, void *data, uint32_t events) {
    return _epoll.add(fd, data, events | EPOLLRDHUP);
}

int PollingThread::Modify(int fd, void *data, uint32_t events) {
    return _epoll.modify(fd, data, events | EPOLLRDHUP);
}

int PollingThread::Delete(int fd, uint32_t events) {
    return _epoll.remove(fd, events | EPOLLRDHUP);
}

}  // namespace raptor
