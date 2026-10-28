#include "wifi_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/wireless.h>
#include <sys/stat.h>

namespace foxd {

WifiEngine& WifiEngine::instance() {
    static WifiEngine eng;
    return eng;
}

WifiEngine::WifiEngine() {
}

WifiEngine::~WifiEngine() {
    if (ctrl_fd_ >= 0) close(ctrl_fd_);
    if (monitor_fd_ >= 0) close(monitor_fd_);
}

bool WifiEngine::initialize() {
    load_samsung_wifi_modules();
    return enable_interface();
}

bool WifiEngine::load_samsung_wifi_modules() {
    // Check rfkill
    std::ofstream rfkill("/sys/class/rfkill/rfkill0/state");
    if (rfkill.is_open()) {
        rfkill << "1\n";
        rfkill.close();
    }

    // Samsung Exynos wlan power triggers if present
    std::ofstream wmt("/dev/wmtWifi");
    if (wmt.is_open()) {
        wmt << "1";
        wmt.close();
    }
    return true;
}

bool WifiEngine::bring_interface_up(const std::string& ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        ioctl(sock, SIOCSIFFLAGS, &ifr);
    }
    close(sock);
    return true;
}

bool WifiEngine::enable_interface() {
    load_samsung_wifi_modules();
    bring_interface_up(ifname_);
    enabled_ = true;
    start_wpa_supplicant_if_needed();
    return true;
}

bool WifiEngine::disable_interface() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifreq ifr;
        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
            ifr.ifr_flags &= ~IFF_UP;
            ioctl(sock, SIOCSIFFLAGS, &ifr);
        }
        close(sock);
    }
    enabled_ = false;
    return true;
}

bool WifiEngine::is_enabled() const {
    return enabled_;
}

bool WifiEngine::start_wpa_supplicant_if_needed() {
    // Candidate socket paths
    const char* paths[] = {
        "/run/wpa_supplicant/wlan0",
        "/data/vendor/wifi/wpa/sockets/wlan0",
        "/data/misc/wifi/sockets/wlan0"
    };

    for (const char* p : paths) {
        if (access(p, F_OK) == 0) {
            return true;
        }
    }

    // In recovery mode, launch wpa_supplicant daemon if not running
    pid_t pid = fork();
    if (pid == 0) {
        mkdir("/run/wpa_supplicant", 0770);
        execl("/system/bin/wpa_supplicant", "wpa_supplicant",
              "-Dnl80211", "-iwlan0", "-C/run/wpa_supplicant", "-B", nullptr);
        _exit(1);
    }
    usleep(250000);
    return true;
}

std::string WifiEngine::send_wpa_cmd(const std::string& cmd) {
    int s = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0) return "";

    struct sockaddr_un local;
    std::memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    std::string client_path = "/tmp/foxd_wpa_" + std::to_string(getpid()) + "_" + std::to_string(rand() % 10000);
    std::strncpy(local.sun_path, client_path.c_str(), sizeof(local.sun_path) - 1);
    unlink(client_path.c_str());

    if (bind(s, (struct sockaddr*)&local, sizeof(local)) < 0) {
        close(s);
        return "";
    }

    const char* targets[] = {
        "/run/wpa_supplicant/wlan0",
        "/data/vendor/wifi/wpa/sockets/wlan0",
        "/data/misc/wifi/sockets/wlan0"
    };

    bool connected = false;
    for (const char* tgt : targets) {
        struct sockaddr_un remote;
        std::memset(&remote, 0, sizeof(remote));
        remote.sun_family = AF_UNIX;
        std::strncpy(remote.sun_path, tgt, sizeof(remote.sun_path) - 1);

        if (sendto(s, cmd.c_str(), cmd.length(), 0, (struct sockaddr*)&remote, sizeof(remote)) > 0) {
            connected = true;
            break;
        }
    }

    std::string reply;
    if (connected) {
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buf[4096];
        ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            reply = buf;
        }
    }

    close(s);
    unlink(client_path.c_str());
    return reply;
}

std::vector<WifiScanResult> WifiEngine::trigger_scan_and_get_results() {
    std::vector<WifiScanResult> results;
    send_wpa_cmd("SCAN");
    usleep(1500000); // Wait 1.5s for scan completion

    std::string res = send_wpa_cmd("SCAN_RESULTS");
    std::istringstream stream(res);
    std::string line;

    // First line is header (bssid / frequency / signal level / flags / ssid)
    if (std::getline(stream, line)) {
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            std::istringstream liness(line);
            WifiScanResult item;
            if (liness >> item.bssid >> item.frequency >> item.signal_level >> item.flags) {
                std::getline(liness, item.ssid);
                if (!item.ssid.empty() && item.ssid[0] == '\t') item.ssid.erase(0, 1);
                results.push_back(item);
            }
        }
    }
    return results;
}

bool WifiEngine::connect_network(const std::string& ssid, const std::string& psk, const std::string& key_mgmt) {
    std::string id_str = send_wpa_cmd("ADD_NETWORK");
    if (id_str.empty() || id_str.find("FAIL") != std::string::npos) {
        return false;
    }
    int net_id = atoi(id_str.c_str());
    if (net_id < 0 && id_str != "0") {
        return false;
    }

    send_wpa_cmd("SET_NETWORK " + std::to_string(net_id) + " ssid \"" + ssid + "\"");
    if (psk.empty()) {
        send_wpa_cmd("SET_NETWORK " + std::to_string(net_id) + " key_mgmt NONE");
    } else {
        send_wpa_cmd("SET_NETWORK " + std::to_string(net_id) + " key_mgmt " + key_mgmt);
        send_wpa_cmd("SET_NETWORK " + std::to_string(net_id) + " psk \"" + psk + "\"");
    }
    send_wpa_cmd("ENABLE_NETWORK " + std::to_string(net_id));
    send_wpa_cmd("SELECT_NETWORK " + std::to_string(net_id));
    send_wpa_cmd("SAVE_CONFIG");

    // Acquire DHCP IP
    acquire_dhcp_lease(ifname_);
    return true;
}

bool WifiEngine::disconnect_network() {
    send_wpa_cmd("DISCONNECT");
    return true;
}

WifiStatus WifiEngine::get_status() {
    WifiStatus s;
    s.enabled = enabled_;
    std::string st = send_wpa_cmd("STATUS");
    std::istringstream stream(st);
    std::string line;

    while (std::getline(stream, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string k = line.substr(0, pos);
        std::string v = line.substr(pos + 1);

        if (k == "wpa_state") s.state = v;
        else if (k == "ssid") s.ssid = v;
        else if (k == "bssid") s.bssid = v;
        else if (k == "ip_address") s.ip_address = v;
    }

    s.connected = (s.state == "COMPLETED");

    // Query IP address directly from interface if not populated by wpa_supplicant
    if (s.ip_address.empty()) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            struct ifreq ifr;
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
                struct sockaddr_in* sin = (struct sockaddr_in*)&ifr.ifr_addr;
                s.ip_address = inet_ntoa(sin->sin_addr);
            }
            close(sock);
        }
    }

    last_status_ = s;
    return s;
}

bool WifiEngine::acquire_dhcp_lease(const std::string& ifname) {
    // In recovery mode, launch dhcp client or assign fallback static address if offline
    pid_t pid = fork();
    if (pid == 0) {
        execl("/system/bin/dhcptool", "dhcptool", ifname.c_str(), nullptr);
        execl("/system/bin/dhcpcd", "dhcpcd", "-ABKL", ifname.c_str(), nullptr);
        _exit(1);
    }
    return true;
}

void WifiEngine::set_event_callback(WifiEventCallback cb) {
    event_cb_ = cb;
}

void WifiEngine::poll_events() {
    // Event monitoring tick
}

} // namespace foxd
