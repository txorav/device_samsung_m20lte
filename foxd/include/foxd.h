#ifndef FOXD_MAIN_H
#define FOXD_MAIN_H

#include "jsonrpc.h"
#include "wifi_engine.h"
#include "scrcpy_server.h"
#include "container_engine.h"
#include "root_suite.h"
#include "app_manager.h"
#include "m20lte_hal.h"

namespace foxd {

class FoxDaemon {
public:
    static FoxDaemon& instance();

    bool initialize(const std::string& socket_path = "/dev/socket/foxd.sock");
    void run();
    void stop();

private:
    FoxDaemon();
    ~FoxDaemon();

    void register_rpc_handlers();
    void client_worker(int client_fd);

    std::string socket_path_;
    int server_fd_ = -1;
    bool running_ = false;
    JsonRpcDispatcher dispatcher_;
};

} // namespace foxd

#endif // FOXD_MAIN_H
