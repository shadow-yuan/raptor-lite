#include <stdint.h>
#include <iostream>

#include "raptor-lite/raptor-lite.h"

class TcpServerTest : public raptor::AcceptorHandler,
                      public raptor::MessageHandler,
                      public raptor::HeartbeatHandler,
                      public raptor::EndpointClosedHandler {
public:
    TcpServerTest(/* args */);
    ~TcpServerTest();

    void OnAccept(const raptor::Endpoint &ep, raptor::Property &settings) {
        settings({{"SocketRecvTimeoutMs", 5000}, {"SocketSendTimeoutMs", 5000}});
        std::cout << "OnAccept:" << ep.PeerString() << std::endl;
        std::cout << "  fd: " << ep.SocketFd() << std::endl;
        std::cout << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        std::cout << "  RemotePort: " << ep.RemotePort() << std::endl;
        std::cout << "  LocalIp: " << ep.LocalIp() << std::endl;
        std::cout << "  LocalPort: " << ep.LocalPort() << std::endl;
        std::cout << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        container->AttachEndpoint(ep);
    }

    int OnMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {
        std::cout << "OnMessage:\n"
                  << "peer:" << ep.PeerString() << "\nmsg:" << msg.ToString() << std::endl;

        std::string str = "Reply:" + msg.ToString();
        ep.SendMsg(str);
        return 0;
    }

    // heart-beat event
    void OnHeartbeat(const raptor::Endpoint &ep) {
        static int64_t current = 0;
        static int i = 0;
        if (current == 0) {
            current = GetCurrentMilliseconds();
            std::cout << "OnHeartbeat:  fd = " << ep.SocketFd() << std::endl;
        } else {
            int64_t now = GetCurrentMilliseconds();
            std::cout << "OnHeartbeat:  fd = " << ep.SocketFd() << ", interval = " << now - current
                      << std::endl;
            current = now;
        }
        ep.SendMsg("This is a server heart-beat message!i:" + std::to_string(i++));
    }

    // millseconds
    size_t GetHeartbeatInterval() {
        return 30 * 1000;
    }

    void OnClosed(const raptor::Endpoint &ep, const raptor::Event &event) {
        std::cout << "OnClosed:" << ep.PeerString() << std::endl;
        std::cout << "  fd: " << ep.SocketFd() << std::endl;
        std::cout << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        std::cout << "  RemotePort: " << ep.RemotePort() << std::endl;
        std::cout << "  LocalIp: " << ep.LocalIp() << std::endl;
        std::cout << "  LocalPort: " << ep.LocalPort() << std::endl;
        std::cout << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        std::cout << "  Event: \n"
                  << "    type: " << event.Type() << "    What: " << event.What() << std::endl;
    }

    void init() {
        raptor::Property p{{"AcceptorHandler", static_cast<AcceptorHandler *>(this)},
                           {"MessageHandler", static_cast<MessageHandler *>(this)},
                           {"HeartbeatHandler", static_cast<HeartbeatHandler *>(this)},
                           {"EndpointClosedHandler", static_cast<EndpointClosedHandler *>(this)}};

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

    void start_and_listening(const std::string &addr) {
        raptor_error err = acceptor->Start();
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "Failed to Start: " << err->ToString() << std::endl;
            return;
        }
        err = container->Start();
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "Failed to Start: " << err->ToString() << std::endl;
            return;
        }
        err = acceptor->AddListening(addr);
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "Failed to Listening: " << err->ToString() << std::endl;
            return;
        }
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
    // don't call raptor function, because TcpServerTest(depend: each handler) not construction
    // completed.
}

TcpServerTest::~TcpServerTest() {
    raptor::DestoryAcceptor(acceptor);
    raptor::DestoryContainer(container);
}

int main() {
    RaptorGlobalStartup();
    std::cout << " ---- prepare start server ---- " << std::endl;
    TcpServerTest server;
    server.init();
    server.start_and_listening("localhost:50051");
    log_error("Press any key to quit");
    getchar();
    server.stop();
    RaptorGlobalCleanup();
    return 0;
}
