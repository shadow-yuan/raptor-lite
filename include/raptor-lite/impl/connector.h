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

#ifndef __RAPTOR_LITE_CONNECTOR__
#define __RAPTOR_LITE_CONNECTOR__

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "raptor-lite/utils/status.h"

namespace raptor {
class Endpoint;
class Property;
class ConnectorHandler {
public:
    virtual ~ConnectorHandler() {}

    /*
     * settings property:
     *   1. SocketNoSIGPIPE     (bool, default:true)
     *   2. SocketReuseAddress  (bool, default:true)
     *   3. SocketRecvTimeout   (int)
     *   4. SocketSendTimeout   (int)
     *   5. SocketLowLatency    (bool, default:true)
     */
    virtual void OnConnect(const Endpoint &ep, Property &settings) = 0;
    virtual void OnErrorOccurred(const Endpoint &ep, raptor_error desc) = 0;
};

class Connector {
public:
    virtual ~Connector() {}
    virtual raptor_error Start() = 0;
    virtual void Shutdown() = 0;
    virtual raptor_error Connect(const std::string &addr) = 0;
};

/*
 * Property:
 *   1. ConnectorHandler (required)
 *   2. ConnecThreadNum  (optional, default:1)
 *   3. TcpUserTimeoutMs (optional, default:0)
 */
raptor_error CreateConnector(const Property &p, Connector **out);
void DestoryConnector(Connector *);
}  // namespace raptor

#endif  // __RAPTOR_LITE_CONNECTOR__
