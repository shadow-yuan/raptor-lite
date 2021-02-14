#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <map>
#include <iostream>
#include <sstream>

#include "raptor-lite/raptor-lite.h"

struct echo_header {
    uint32_t payload_size;
    uint32_t request_id;
};

raptor::AtomicInt32 g_endpoint_counter(0);

class HttpEchoServer : public raptor::AcceptorHandler,
                       public raptor::MessageHandler,
                       public raptor::ProtocolHandler,
                       public raptor::EndpointClosedHandler {
public:
    HttpEchoServer(/* args */);
    ~HttpEchoServer();

    void OnAccept(const raptor::Endpoint &ep, raptor::Property &settings) {
        settings({{"SocketRecvTimeoutMs", 5000}, {"SocketSendTimeoutMs", 5000}});
        g_endpoint_counter.FetchAdd(1);
        // log_debug("OnAccept:%d -> %s", g_endpoint_counter.Load(), ep.PeerString().c_str());
        container->AttachEndpoint(ep);
    }

    int OnCheckPackageLength(const raptor::Endpoint &ep, const void *data, size_t len) {
        if (len < sizeof(echo_header)) return 0;
        auto p = reinterpret_cast<const echo_header *>(data);
        return sizeof(echo_header) + p->payload_size;
    }

    int OnMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {

        // log_debug("Onmessage:\n%s", reinterpret_cast<const char *>(msg.begin()));
        auto request = reinterpret_cast<const echo_header *>(msg.begin());

        const char *RESPONSE = "Response:";
        const int RESPONSE_LEN = 9;

        raptor::Slice rsp_slice =
            raptor::MakeSliceByLength(request->payload_size + sizeof(echo_header) + RESPONSE_LEN);

        auto response = reinterpret_cast<echo_header *>(rsp_slice.Buffer());
        response->payload_size = request->payload_size + RESPONSE_LEN;
        response->request_id = request->request_id;
        memcpy(response + 1, RESPONSE, RESPONSE_LEN);
        memcpy(rsp_slice.Buffer() + sizeof(echo_header) + RESPONSE_LEN,
               msg.begin() + sizeof(echo_header), request->payload_size);
        ep.SendMsg(rsp_slice);
        return 0;
    }

    void OnClosed(const raptor::Endpoint &ep, const raptor::Event &event) {
        // log_debug("OnClosed: %s", ep.PeerString().c_str());
    }

    void init() {

        uint32_t cpus = raptor_get_number_of_cpu_cores();

        raptor::Property p{
            {"AcceptorHandler", static_cast<raptor::AcceptorHandler *>(this)},
            {"MessageHandler", static_cast<raptor::MessageHandler *>(this)},
            {"ProtocolHandler", static_cast<raptor::ProtocolHandler *>(this)},
            {"EndpointClosedHandler", static_cast<raptor::EndpointClosedHandler *>(this)},
            {"NotCheckConnectionTimeout", true},
            {"RecvSendThreads", cpus},
            {"MQConsumerThreads", 1}};

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
            log_error("Failed to start acceptor: %s", err->ToString().c_str());
            return;
        }
        err = container->Start();
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to start container: %s", err->ToString().c_str());
            return;
        }
        err = acceptor->AddListening(addr);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to listening %s, error: %s", addr.c_str(), err->ToString().c_str());
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

HttpEchoServer::HttpEchoServer(/* args */) {
    // don't call raptor function, because HttpEchoServer(depend: each handler) not construction
    // completed.
}

HttpEchoServer::~HttpEchoServer() {
    raptor::DestoryAcceptor(acceptor);
    raptor::DestoryContainer(container);
}

int main() {
    printf(" ---- prepare start echo server ---- \n");
    RaptorGlobalStartup();
    HttpEchoServer server;
    server.init();
    server.start_and_listening("0.0.0.0:50051");
    log_warn("Press any key to quit");
    getchar();
    server.stop();
    RaptorGlobalCleanup();
    return 0;
}
