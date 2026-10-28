#include "foxd.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <signal.h>
#include <thread>

namespace foxd {

FoxDaemon& FoxDaemon::instance() {
    static FoxDaemon d;
    return d;
}

FoxDaemon::FoxDaemon() {
}

FoxDaemon::~FoxDaemon() {
    stop();
}

void FoxDaemon::register_rpc_handlers() {
    // 1. System status
    dispatcher_.register_method("m20lte.system.getStatus", [](const std::string&) -> std::string {
        auto part_mode = M20LteHal::instance().detect_partition_mode();
        std::string pmode_str = (part_mode == PartitionTargetMode::RETROFIT_DYNAMIC) ? "retrofit_dynamic" :
                               (part_mode == PartitionTargetMode::UNIFIED_SUPER_GPT) ? "unified_super_gpt" : "non_dynamic_physical";

        auto fl = M20LteHal::instance().get_flashlight_state();
        auto root = RootSuite::instance().detect_root_solution();
        auto wstate = WifiEngine::instance().get_status();

        std::ostringstream ss;
        ss << "{"
           << "\"device\":\"m20lte\","
           << "\"soc\":\"exynos7904\","
           << "\"partition_mode\":\"" << pmode_str << "\","
           << "\"flashlight\":{\"enabled\":" << json_bool(fl.enabled) << ",\"level\":" << fl.level << ",\"raw_value\":" << fl.raw_value << "},"
           << "\"wifi\":{\"enabled\":" << json_bool(wstate.enabled) << ",\"connected\":" << json_bool(wstate.connected)
           << ",\"ssid\":\"" << json_escape(wstate.ssid) << "\",\"ip\":\"" << wstate.ip_address << "\"},"
           << "\"scrcpy\":{\"running\":" << json_bool(ScrcpyServer::instance().is_running()) << "},"
           << "\"root\":{\"type\":\"" << root.type << "\",\"active\":" << json_bool(root.active) << ",\"modules\":" << root.module_count << "}"
           << "}";
        return ss.str();
    });

    // 2. Wi-Fi API
    dispatcher_.register_method("wifi.scan", [](const std::string&) -> std::string {
        auto list = WifiEngine::instance().trigger_scan_and_get_results();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"ssid\":\"" << json_escape(list[i].ssid) << "\","
               << "\"bssid\":\"" << list[i].bssid << "\","
               << "\"signal\":" << list[i].signal_level << ","
               << "\"flags\":\"" << json_escape(list[i].flags) << "\"}";
        }
        ss << "]";
        return ss.str();
    });

    dispatcher_.register_method("wifi.getStatus", [](const std::string&) -> std::string {
        auto s = WifiEngine::instance().get_status();
        return "{\"enabled\":" + json_bool(s.enabled) + ",\"connected\":" + json_bool(s.connected) +
               ",\"ssid\":\"" + json_escape(s.ssid) + "\",\"ip\":\"" + s.ip_address + "\"}";
    });

    dispatcher_.register_method("wifi.enable", [](const std::string&) -> std::string {
        bool ok = WifiEngine::instance().enable_interface();
        return "{\"success\":" + json_bool(ok) + "}";
    });

    dispatcher_.register_method("wifi.disable", [](const std::string&) -> std::string {
        bool ok = WifiEngine::instance().disable_interface();
        return "{\"success\":" + json_bool(ok) + "}";
    });

    // 3. Hardware Flashlight API
    dispatcher_.register_method("hardware.setFlashlight", [](const std::string& params) -> std::string {
        bool enable = (params.find("\"enable\":true") != std::string::npos || params.find("\"enable\": true") != std::string::npos);
        int lvl = 5;
        auto p = params.find("\"level\":");
        if (p != std::string::npos) {
            lvl = atoi(params.c_str() + p + 8);
        }
        bool ok = M20LteHal::instance().set_flashlight(enable, lvl);
        return "{\"success\":" + json_bool(ok) + "}";
    });

    // 4. Scrcpy & Wireless ADB
    dispatcher_.register_method("scrcpy.start", [](const std::string&) -> std::string {
        bool ok = ScrcpyServer::instance().start_server();
        return "{\"success\":" + json_bool(ok) + "}";
    });

    dispatcher_.register_method("scrcpy.stop", [](const std::string&) -> std::string {
        bool ok = ScrcpyServer::instance().stop_server();
        return "{\"success\":" + json_bool(ok) + "}";
    });

    dispatcher_.register_method("scrcpy.enableWirelessAdb", [](const std::string& params) -> std::string {
        int port = 5555;
        auto p = params.find("\"port\":");
        if (p != std::string::npos) {
            port = atoi(params.c_str() + p + 7);
        }
        bool ok = ScrcpyServer::instance().enable_wireless_adb(port);
        return "{\"success\":" + json_bool(ok) + ",\"port\":" + std::to_string(port) + "}";
    });

    // 5. Container Engine
    dispatcher_.register_method("container.list", [](const std::string&) -> std::string {
        auto list = ContainerEngine::instance().list_containers();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"id\":\"" << list[i].container_id << "\",\"state\":\"" << list[i].state << "\"}";
        }
        ss << "]";
        return ss.str();
    });

    // 6. Root Suite
    dispatcher_.register_method("root.detect", [](const std::string&) -> std::string {
        auto r = RootSuite::instance().detect_root_solution();
        return "{\"type\":\"" + r.type + "\",\"mode\":\"" + r.mode + "\",\"version\":\"" + r.version + "\"}";
    });

    dispatcher_.register_method("root.listModules", [](const std::string&) -> std::string {
        auto list = RootSuite::instance().list_modules();
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"id\":\"" << list[i].id << "\",\"name\":\"" << json_escape(list[i].name)
               << "\",\"enabled\":" << json_bool(list[i].enabled) << "}";
        }
        ss << "]";
        return ss.str();
    });

    // 7. App Manager
    dispatcher_.register_method("app.list", [](const std::string&) -> std::string {
        auto list = AppManager::instance().list_installed_apps(false);
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"package\":\"" << list[i].package_name << "\",\"uid\":" << list[i].uid << "}";
        }
        ss << "]";
        return ss.str();
    });
}

bool FoxDaemon::initialize(const std::string& socket_path) {
    socket_path_ = socket_path;
    register_rpc_handlers();

    M20LteHal::instance().initialize();
    WifiEngine::instance().initialize();
    ScrcpyServer::instance().initialize();
    ScrcpyServer::instance().start_server();
    ContainerEngine::instance().initialize();

    unlink(socket_path_.c_str());
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) return false;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    chmod(socket_path_.c_str(), 0666);
    listen(server_fd_, 8);
    running_ = true;
    return true;
}

void FoxDaemon::client_worker(int client_fd) {
    char buf[4096];
    std::string acc;

    while (running_) {
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        acc += buf;

        auto nl = acc.find('\n');
        while (nl != std::string::npos) {
            std::string line = acc.substr(0, nl);
            acc.erase(0, nl + 1);

            if (!line.empty()) {
                std::string reply = dispatcher_.dispatch(line);
                if (!reply.empty()) {
                    write(client_fd, reply.c_str(), reply.length());
                }
            }
            nl = acc.find('\n');
        }
    }
    close(client_fd);
}

void FoxDaemon::run() {
    while (running_) {
        int client = accept(server_fd_, nullptr, nullptr);
        if (client < 0) {
            if (!running_) break;
            continue;
        }
        std::thread(&FoxDaemon::client_worker, this, client).detach();
    }
}

void FoxDaemon::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    unlink(socket_path_.c_str());
}

} // namespace foxd

int main(int argc, char** argv) {
    (void)argc;
    signal(SIGPIPE, SIG_IGN);

    // If invoked as app_process (from scrcpy PC client)
    if (argv && argv[0] && std::strstr(argv[0], "app_process") != nullptr) {
        std::cout << "[FoxUI] Native Recovery scrcpy runner active for m20lte\n";
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_un addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, "/dev/socket/foxd.sock", sizeof(addr.sun_path) - 1);
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                const char* cmd = "{\"jsonrpc\":\"2.0\",\"method\":\"scrcpy.start\",\"id\":1}\n";
                write(sock, cmd, strlen(cmd));
            }
            close(sock);
        }
        while (true) {
            sleep(10);
        }
        return 0;
    }

    std::cout << "Starting FoxUI Control Suite Daemon (foxd) for m20lte...\n";
    if (!foxd::FoxDaemon::instance().initialize()) {
        std::cerr << "Failed to initialize foxd on /dev/socket/foxd.sock\n";
        return 1;
    }
    std::cout << "foxd initialized successfully. Running event loop.\n";
    foxd::FoxDaemon::instance().run();
    return 0;
}
