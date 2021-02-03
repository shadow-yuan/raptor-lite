#include <stdint.h>
#include <iostream>

#include "raptor-lite/impl/acceptor.h"
#include "raptor-lite/impl/container.h"
#include "raptor-lite/impl/handler.h"
#include "raptor-lite/impl/property.h"
#include "raptor-lite/impl/endpoint.h"

#include "raptor-lite/utils/testutil.h"
#include "raptor-lite/utils/slice.h"
#include "raptor-lite/utils/slice_buffer.h"

class ConnectorTestUtil {};

class TcpServerTest : public raptor::AcceptorHandler, public raptor::MessageHandler {
public:
    TcpServerTest(/* args */);
    ~TcpServerTest();

    void OnAccept(const raptor::Endpoint &ep, raptor::Property &settings) {
        std::cout << "OnAccept:" << ep.PeerString() << std::endl;
        container->AttachEndpoint(ep);
    }

    int OnMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {
        std::cout << "OnMessage:\n"
                  << "peer:" << ep.PeerString() << "\nmsg:" << msg.ToString() << std::endl;
        return 0;
    }

    void start() {
        acceptor->Start();
        container->Start();
    }

    void stop() {
        acceptor->Shutdown();
        container->Shutdown();
    }

private:
    raptor::Container *container = nullptr;
    raptor::Acceptor *acceptor = nullptr;
};

TcpServerTest::TcpServerTest(/* args */) {
    raptor::Property p{{"MessageHandler", this}, {"AcceptorHandler", this}};
    raptor_error err = raptor::CreateContainer(p, &container);
    if (err != RAPTOR_ERROR_NONE) {
        std::cout << "Failed to create container: " << err->ToString() << std::endl;
        abort();
    }
    err = raptor::CreateAcceptor(p, &acceptor);
    if (err != RAPTOR_ERROR_NONE) {
        std::cout << "Failed to create acceptor: " << err->ToString() << std::endl;
        abort();
    }
}

TcpServerTest::~TcpServerTest() {
    raptor::DestoryAcceptor(acceptor);
    raptor::DestoryContainer(container);
}

int main(int argc, char *argv[]) {

    getchar();
    return 0;
}
