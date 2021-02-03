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

#include "raptor-lite/impl/container.h"
#include <memory>
#ifdef _WIN32
#include "src/windows/tcp_container.h"
#else
#include "src/linux/tcp_container.h"
#endif
#include "raptor-lite/utils/status.h"
#include "raptor-lite/impl/handler.h"
#include "raptor-lite/impl/property.h"

namespace raptor {
class ContainerAdaptor : public Container {

public:
    explicit ContainerAdaptor(MessageHandler *handler);
    ~ContainerAdaptor();

    raptor_error Init(int rs_threads = 1);
    void SetProtocolHandler(ProtocolHandler *p);
    void SetHeartbeatHandler(HeartbeatHandler *h);
    void SetEventHandler(EventHandler *e);

    bool Start() override;
    void Shutdown() override;
    void AttachEndpoint(Endpoint *) override;
    bool SendMsg(Endpoint *ep, const void *data, size_t len) override;

private:
    std::shared_ptr<TcpContainer> _impl;
};

ContainerAdaptor::ContainerAdaptor(MessageHandler *handler)
    : _impl(std::make_shared<TcpContainer>(handler)) {}

ContainerAdaptor::~ContainerAdaptor() {}

raptor_error ContainerAdaptor::Init(int rs_threads) {
    return _impl->Init(rs_threads);
}

bool ContainerAdaptor::Start() {
    return _impl->Start();
}
void ContainerAdaptor::Shutdown() {
    _impl->Shutdown();
}
void ContainerAdaptor::AttachEndpoint(Endpoint *ep) {
    _impl->AttachEndpoint(ep);
}

bool ContainerAdaptor::SendMsg(Endpoint *ep, const void *data, size_t len) {
    return _impl->SendMsg(ep, data, len);
}

/*
 * Property:
 *   1. ProtocolHandler (optional)
 *   2. MessageHandler  (required)
 *   3. HeartbeatHandler(optional)
 *   4. EventHandler    (optional)
 *   5. RecvSendThreads (optional, default: 1)
 */
raptor_error CreateContainer(const Property &p, Container **out) {
    MessageHandler *message =
        reinterpret_cast<MessageHandler *>(p.GetValue<intptr_t>("MessageHandler", 0));

    if (!message) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("Missing MessageHandler");
    }

    *out = nullptr;

    ProtocolHandler *proto =
        reinterpret_cast<ProtocolHandler *>(p.GetValue<intptr_t>("ProtocolHandler", 0));

    HeartbeatHandler *heartbeat =
        reinterpret_cast<HeartbeatHandler *>(p.GetValue<intptr_t>("HeartbeatHandler", 0));
    EventHandler *event = reinterpret_cast<EventHandler *>(p.GetValue<intptr_t>("EventHandler", 0));

    int RecvSendThreads = p.GetValue("RecvSendThreads", 1);

    ContainerAdaptor *adaptor = new ContainerAdaptor(message);
    raptor_error e = adaptor->Init(RecvSendThreads);
    if (e == RAPTOR_ERROR_NONE) {
        *out = adaptor;
        adaptor->SetProtocolHandler(proto);
        adaptor->SetHeartbeatHandler(heartbeat);
        adaptor->SetEventHandler(event);
    }
    return e;
}

void DestoryContainer(Container *cc) {
    if (cc) {
        cc->Shutdown();
        delete cc;
    }
}
}  // namespace raptor
