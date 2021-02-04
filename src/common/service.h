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

#ifndef __RAPTOR_CORE_SERVICE__
#define __RAPTOR_CORE_SERVICE__

#include <stddef.h>
#include <stdint.h>

namespace raptor {

class Slice;
class Endpoint;
class Event;

namespace internal {

// for epoll
class IEpollReceiver {
public:
    virtual ~IEpollReceiver() {}
    virtual void OnErrorEvent(void *ptr) = 0;
    virtual void OnRecvEvent(void *ptr) = 0;
    virtual void OnSendEvent(void *ptr) = 0;
    virtual void OnTimeoutCheck(int64_t current_millseconds) = 0;
};

// for iocp
class IIocpReceiver {
public:
    virtual ~IIocpReceiver() {}
    virtual void OnErrorEvent(void *ptr, size_t err_code) = 0;
    virtual void OnRecvEvent(void *ptr, size_t transferred_bytes, uint32_t handle_id) = 0;
    virtual void OnSendEvent(void *ptr, size_t transferred_bytes, uint32_t handle_id) = 0;
    virtual void OnTimeoutCheck(int64_t current_millseconds) = 0;
};

class INotificationTransfer {
public:
    virtual ~INotificationTransfer() {}
    virtual void OnDataReceived(const Endpoint &ep, const Slice &s) = 0;
    virtual void OnClosed(const Endpoint &ep, const Event& event) = 0;
};

}  // namespace internal
}  // namespace raptor

#endif  // __RAPTOR_CORE_SERVICE__
