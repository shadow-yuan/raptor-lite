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

class Endpoint;
class Event;
class Slice;
struct EventDetail;

namespace internal {

constexpr int kErrorEvent = 1;
constexpr int kSendEvent = 1 << 1;
constexpr int kRecvEvent = 1 << 2;
constexpr int kConnectEvent = 1 << 3;
constexpr int kAcceptEvent = 1 << 4;

class EventReceivingService {
public:
    virtual ~EventReceivingService() {}
    virtual void OnEventProcess(EventDetail *detail) = 0;
    virtual void OnTimeoutCheck(int64_t current_millseconds) = 0;
};

class NotificationTransferService {
public:
    virtual ~NotificationTransferService() {}
    virtual void OnDataReceived(const Endpoint &ep, const Slice &s) = 0;
    virtual void OnClosed(const Endpoint &ep, const Event &event) = 0;
};

}  // namespace internal
}  // namespace raptor

#endif  // __RAPTOR_CORE_SERVICE__
