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

namespace raptor {
class Endpoint;
class Property;
class Container {
public:
    virtual ~Container() {}
    virtual bool Start() = 0;
    virtual void Shutdown() = 0;
    virtual void AttachEndpoint(Endpoint *) = 0;
    virtual bool SendMsg(Endpoint *ep, void *data, size_t len) = 0;
};

/*
 * Property:
 *   1. ProtocolHandler (optional)
 *   2. MessageHandler  (required)
 *   3. HeartbeatHandler(optional)
 *   4. EventHandler    (optional)
 */
Container *CreateContainer(const Property &p);
void DestoryContainer(Container *);
}  // namespace raptor
#endif  // __RAPTOR_LITE_CONTAINER__
