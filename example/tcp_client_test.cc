#include <stdint.h>
#include <iostream>

#include "raptor-lite/impl/connector.h"
#include "raptor-lite/impl/property.h"
#include "raptor-lite/utils/testutil.h"

class ConnectorTestUtil {};
raptor::Connector *cc = nullptr;

class ClientHandler : public raptor::ConnectorHandler {
public:
    ClientHandler(/* args */);
    ~ClientHandler();

    void OnConnect(const raptor::Endpoint &ep, raptor::Property &settings) {}

    void OnErrorOccurred(const raptor::Endpoint &ep, raptor_error desc) {
        std::cout << "OnErrorOccurred:\n  desc: " << desc->ToString() << std::endl;
    }
};

ClientHandler ::ClientHandler(/* args */) {}

ClientHandler ::~ClientHandler() {}

ClientHandler client;

int main(int argc, char *argv[]) {
    raptor::Property p{{"ConnectorHandler", &client}};
    raptor_error err = raptor::CreateConnector(p, &cc);

    getchar();
    raptor::DestoryConnector(cc);
    return 0;
}
