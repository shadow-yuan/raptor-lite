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

#ifndef __RAPTOR_LITE_CONTAINER__
#define __RAPTOR_LITE_CONTAINER__

#include <stddef.h>
#include <stdint.h>

#include "raptor-lite/utils/status.h"

namespace raptor {
class Endpoint;
class Property;
class Container {
public:
    virtual ~Container() {}
    virtual raptor_error Start() = 0;
    virtual void Shutdown() = 0;
    virtual raptor_error AttachEndpoint(const Endpoint &ep) = 0;
    virtual bool SendMsg(const Endpoint &ep, const void *data, size_t len) = 0;
    virtual void CloseEndpoint(const Endpoint &ep, bool event_notify = false) = 0;
};

/*
 * Property:
 *   1. ProtocolHandler            (optional)
 *   2. MessageHandler             (required)
 *   3. HeartbeatHandler           (optional)
 *   4. EventHandler               (optional)
 *   5. RecvSendThreads            (optional, default: 1)
 *   6. DefaultContainerSize       (optional, default: 256)
 *   7. MaxContainerSize           (optional, default: 1048576)
 *   8. NotCheckConnectionTimeout  (optional, default: false)
 *   9. ConnectionTimeoutMs        (optional, default: 60000)
 *  10. MQConsumerThreads          (optional, default: 1)
 */
raptor_error CreateContainer(const Property &p, Container **out);
void DestoryContainer(Container *);
}  // namespace raptor
#endif  // __RAPTOR_LITE_CONTAINER__
