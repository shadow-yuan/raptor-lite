#include <stdint.h>
#include <string.h>

#include <iostream>
#include <thread>

#include "raptor-lite/raptor-lite.h"

raptor::Mutex g_mtx;
raptor::ConditionVariable g_cv;

class ClientHandler : public raptor::ConnectorHandler {
public:
    ClientHandler(/* args */);
    ~ClientHandler();

    void OnConnect(const raptor::Endpoint &ep, raptor::Property &settings) {
        settings({{"SocketNonBlocking", false},
                  {"SocketRecvTimeoutMs", 5000},
                  {"SocketSendTimeoutMs", 5000}});
        std::cout << "OnConnect:" << ep.PeerString() << std::endl;
        std::cout << "  fd: " << ep.SocketFd() << std::endl;
        std::cout << "  RemoteIp: " << ep.RemoteIp() << std::endl;
        std::cout << "  RemotePort: " << ep.RemotePort() << std::endl;
        std::cout << "  LocalIp: " << ep.LocalIp() << std::endl;
        std::cout << "  LocalPort: " << ep.LocalPort() << std::endl;
        std::cout << "  ConnectionId: " << ep.ConnectionId() << std::endl;
        _ep = ep;
        g_cv.Signal();
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
        _ep.Close(false);
        cc->Shutdown();
    }

    int Send(const std::string &msg) {
        return _ep.SyncSend(msg.data(), msg.size());
    }

    int Recv(char *buf, size_t len) {
        return _ep.SyncRecv(buf, len);
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
    g_cv.Wait(&g_mtx);
    log_debug("prepare send first request");
    int r = client.Send("FirstRequest");
    log_debug("client.send return %d", r);
    char buf[512] = {0};
    r = client.Recv(buf, sizeof(buf));
    log_debug("client.recv return %d, %s", r, buf);

    log_debug("prepare send second request");
    r = client.Send("SecondRequest");
    log_debug("client.send return %d", r);
    memset(buf, 0, sizeof(buf));
    r = client.Recv(buf, sizeof(buf));
    log_debug("client.recv return %d, %s", r, buf);

    for (int i = 0; i < 30; i++) {
        memset(buf, 0, sizeof(buf));
        r = client.Recv(buf, sizeof(buf));
        if (r > 0) log_debug("client.recv return %d, %s", r, buf);
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }

    log_error("Press any key to quit");
    getchar();
    client.stop();
    RaptorGlobalCleanup();
    return 0;
}
