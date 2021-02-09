#include <stdint.h>
#include <iostream>
#include <sstream>

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
        std::stringstream ss;
        ss << "OnAccept:" << ep.PeerString() << std::endl;
        ss << "  fd: " << ep.SocketFd() << std::endl;
        ss << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        ss << "  RemotePort: " << ep.RemotePort() << std::endl;
        ss << "  LocalIp: " << ep.LocalIp() << std::endl;
        ss << "  LocalPort: " << ep.LocalPort() << std::endl;
        ss << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        log_debug("%s", ss.str().c_str());
        container->AttachEndpoint(ep);
    }

    int OnMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {
        std::stringstream ss;
        ss << "OnMessage:\n"
           << "peer:" << ep.PeerString() << "\nmsg:" << msg.ToString() << std::endl;

        log_debug("%s", ss.str().c_str());
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
            log_debug("OnHeartbeat:  fd = %lld", ep.SocketFd());
        } else {
            int64_t now = GetCurrentMilliseconds();
            log_debug("OnHeartbeat:  fd = %lld, interval = %lld", ep.SocketFd(), now - current);
            current = now;
        }
        ep.SendMsg("This is a server heart-beat message!i:" + std::to_string(i++));
    }

    // millseconds
    size_t GetHeartbeatInterval() {
        return 30 * 1000;
    }

    void OnClosed(const raptor::Endpoint &ep, const raptor::Event &event) {
        std::stringstream ss;
        ss << "OnClosed:" << ep.PeerString() << std::endl;
        ss << "  fd: " << ep.SocketFd() << std::endl;
        ss << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        ss << "  RemotePort: " << ep.RemotePort() << std::endl;
        ss << "  LocalIp: " << ep.LocalIp() << std::endl;
        ss << "  LocalPort: " << ep.LocalPort() << std::endl;
        ss << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        ss << "  Event: \n    type: " << event.Type() << "    error_code: " << event.ErrorCode()
           << std::endl;
        log_debug("%s", ss.str().c_str());
    }

    void init() {
        raptor::Property p{{"AcceptorHandler", static_cast<AcceptorHandler *>(this)},
                           {"MessageHandler", static_cast<MessageHandler *>(this)},
                           {"HeartbeatHandler", static_cast<HeartbeatHandler *>(this)},
                           {"EndpointClosedHandler", static_cast<EndpointClosedHandler *>(this)}};

        raptor_error err = raptor::CreateContainer(p, &container);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to create container: %s", err->ToString().c_str());
            abort();
        }
        err = raptor::CreateAcceptor(p, &acceptor);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to create acceptor: %s", err->ToString().c_str());
            abort();
        }
    }

    void start_and_listening(const std::string &addr) {
        raptor_error err = acceptor->Start();
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to Start: %s", err->ToString().c_str());
            return;
        }
        err = container->Start();
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to Start: %s", err->ToString().c_str());
            return;
        }
        err = acceptor->AddListening(addr);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to Listening: %s", err->ToString().c_str());
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
    printf(" ---- prepare start server ---- \n");
    RaptorGlobalStartup();
    TcpServerTest server;
    server.init();
    server.start_and_listening("localhost:50051");
    log_error("Press any key to quit");
    getchar();
    server.stop();
    RaptorGlobalCleanup();
    return 0;
}
