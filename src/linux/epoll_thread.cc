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
EpollThread::EpollThread(internal::IEpollReceiver *rcv)
    : _receiver(rcv)
    , _shutdown(true)
    , _enable_timeout_check(false)
    , _number_of_threads(0)
    , _running_threads(0) {}

EpollThread::~EpollThread() {
    Shutdown();
}

RefCountedPtr<Status> EpollThread::Init(int threads) {
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
            Thread("epoll:thread", std::bind(&EpollThread::DoWork, this, std::placeholders::_1),
                   nullptr, &success);

        if (!success) {
            break;
        }

        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("EpollThread failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error EpollThread::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("EpollThread is not initialized");
    }

    for (int i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void EpollThread::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        for (int i = 0; i < _running_threads; i++) {
            _threads[i].Join();
        }
        delete[] _threads;
        _epoll.shutdown();
    }
}

void EpollThread::EnableTimeoutCheck(bool b) {
    _enable_timeout_check = b;
}

void EpollThread::DoWork(void *ptr) {
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

            if (ev->events & EPOLLERR || ev->events & EPOLLHUP || ev->events & EPOLLRDHUP) {
                _receiver->OnErrorEvent(ev->data.ptr);
                continue;
            }
            if (ev->events & EPOLLIN) {
                _receiver->OnRecvEvent(ev->data.ptr);
            }
            if (ev->events & EPOLLOUT) {
                _receiver->OnSendEvent(ev->data.ptr);
            }
        }
    }
}

int EpollThread::Add(int fd, void *data, uint32_t events) {
    return _epoll.add(fd, data, events | EPOLLRDHUP);
}

int EpollThread::Modify(int fd, void *data, uint32_t events) {
    return _epoll.modify(fd, data, events | EPOLLRDHUP);
}

int EpollThread::Delete(int fd, uint32_t events) {
    return _epoll.remove(fd, events | EPOLLRDHUP);
}

}  // namespace raptor
