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

#include "raptor-lite/impl/connector.h"

#include <memory>

#include "raptor-lite/impl/property.h"
#ifdef _WIN32
#include "src/windows/tcp_connector.h"
#else
#include "src/linux/tcp_connector.h"
#endif

namespace raptor {
class ConnectorAdaptor : public Connector {
public:
    explicit ConnectorAdaptor(ConnectorHandler *handler);
    ~ConnectorAdaptor();

    raptor_error Init(int threads = 1, int timeoutms = 0);
    raptor_error Start() override;
    void Shutdown() override;
    raptor_error Connect(const std::string &addr, intptr_t user = 0) override;

private:
    std::unique_ptr<TcpConnector> _impl;
};

ConnectorAdaptor::ConnectorAdaptor(ConnectorHandler *handler)
    : _impl(new TcpConnector(handler)) {}

ConnectorAdaptor::~ConnectorAdaptor() {}

raptor_error ConnectorAdaptor::Init(int threads, int timeoutms) {
    return _impl->Init(threads, timeoutms);
}

raptor_error ConnectorAdaptor::Start() {
    return _impl->Start();
}

void ConnectorAdaptor::Shutdown() {
    _impl->Shutdown();
}

raptor_error ConnectorAdaptor::Connect(const std::string &addr, intptr_t user) {
    return _impl->Connect(addr, user);
}

/*
 * Property:
 *   1. ConnectorHandler (required)
 *   2. ConnecThreadNum  (optional, default:1)
 *   3. TcpUserTimeoutMs (optional, default:0)
 */
raptor_error CreateConnector(const Property &p, Connector **out) {
    ConnectorHandler *handler =
        reinterpret_cast<ConnectorHandler *>(p.GetValue<intptr_t>("ConnectorHandler"));

    int ConnecThreadNum = p.GetValue("ConnecThreadNum", 1);
    int TcpUserTimeoutMs = p.GetValue("TcpUserTimeoutMs", 0);

    *out = nullptr;

    if (!handler) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("Missing ConnectorHandler");
    }

    ConnectorAdaptor *adaptor = new ConnectorAdaptor(handler);
    raptor_error e = adaptor->Init(ConnecThreadNum, TcpUserTimeoutMs);
    if (e == RAPTOR_ERROR_NONE) {
        *out = adaptor;
    }
    return e;
}

void DestroyConnector(Connector *c) {
    if (c) {
        c->Shutdown();
        delete c;
    }
}

}  // namespace raptor
