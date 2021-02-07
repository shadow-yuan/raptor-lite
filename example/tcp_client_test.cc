#include <stdint.h>
#include <iostream>

#include "raptor-lite/raptor-lite.h"

class ClientHandler : public raptor::ConnectorHandler {
public:
    ClientHandler(/* args */);
    ~ClientHandler();

    void OnConnect(const raptor::Endpoint &ep, raptor::Property &settings) {
        settings({{"SocketRecvTimeoutMs", 5000}, {"SocketSendTimeoutMs", 5000}});
        std::cout << "OnConnect:" << ep.PeerString() << std::endl;
        std::cout << "  fd: " << ep.SocketFd() << std::endl;
        std::cout << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        std::cout << "  RemotePort: " << ep.RemotePort() << std::endl;
        std::cout << "  LocalIp: " << ep.LocalIp() << std::endl;
        std::cout << "  LocalPort: " << ep.LocalPort() << std::endl;
        std::cout << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        _ep = ep;
    }

    void OnErrorOccurred(const raptor::Endpoint &ep, raptor_error desc) {
        std::cout << "OnErrorOccurred:\n  "
                  << "fd: " << ep.SocketFd() << "\n  desc: " << desc->ToString() << std::endl;
    }

    void init() {
        raptor::Property p{{"ConnectorHandler", static_cast<ConnectorHandler *>(this)}};
        raptor_error err = raptor::CreateConnector(p, &cc);
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "CreateConnector: " << err->ToString() << std::endl;
        }
    }

    void start_and_connecting(const std::string &addr) {
        raptor_error err = cc->Start();
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "Failed to Start: " << err->ToString() << std::endl;
            return;
        }
        err = cc->Connect(addr);
        if (err != RAPTOR_ERROR_NONE) {
            std::cout << "Failed to Connect: " << err->ToString() << std::endl;
            return;
        }
    }

    void stop() {
        cc->Shutdown();
    }

private:
    raptor::Connector *cc = nullptr;
    raptor::Endpoint _ep;
};

ClientHandler ::ClientHandler(/* args */)
    : _ep(nullptr) {}

ClientHandler ::~ClientHandler() {
    raptor::DestoryConnector(cc);
}

int main() {
    RaptorGlobalStartup();
    std::cout << " ---- prepare start client ---- " << std::endl;
    ClientHandler client;
    client.init();
    client.start_and_connecting("localhost:50051");
    getchar();
    client.stop();
    RaptorGlobalCleanup();
    return 0;
}
