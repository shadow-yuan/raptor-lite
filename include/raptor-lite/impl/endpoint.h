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

#ifndef __RAPTOR_LITE_ENDPOINT__
#define __RAPTOR_LITE_ENDPOINT__

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>

namespace raptor {
class Container;
class Slice;
class EndpointImpl;
class Endpoint final {
public:
    explicit Endpoint(std::shared_ptr<EndpointImpl> impl);
    ~Endpoint();

    uint64_t ConnectionId() const;

    // don't close it
    uint64_t SocketFd() const;

    std::string PeerString() const;
    bool SendMsg(const Slice &slice) const;
    bool SendMsg(void *data, size_t len) const;
    bool Close() const;

    const std::string &LocalIp() const;
    uint16_t LocalPort() const;
    const std::string &RemoteIp() const;
    uint16_t RemotePort() const;

private:
    std::shared_ptr<EndpointImpl> _impl;
};
}  // namespace raptor

#endif  // __RAPTOR_LITE_ENDPOINT__