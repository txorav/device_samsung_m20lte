#ifndef FOXD_WIFI_ENGINE_H
#define FOXD_WIFI_ENGINE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace foxd {

struct WifiScanResult {
    std::string bssid;
    int frequency = 0;
    int signal_level = 0;
    std::string flags;
    std::string ssid;
};

struct WifiStatus {
    bool enabled = false;
    bool connected = false;
    std::string ssid;
    std::string bssid;
    std::string ip_address;
    std::string gateway;
    std::string dns1;
    int rssi = 0;
    int link_speed = 0;
    std::string state;
    bool internet_verified = false;
};

using WifiEventCallback = std::function<void(const std::string& event, const std::string& details)>;

class WifiEngine {
public:
    static WifiEngine& instance();

    bool initialize();
    bool enable_interface();
    bool disable_interface();
    bool is_enabled() const;

    std::vector<WifiScanResult> trigger_scan_and_get_results();
    bool connect_network(const std::string& ssid, const std::string& psk, const std::string& key_mgmt = "WPA-PSK");
    bool disconnect_network();
    WifiStatus get_status();

    void set_event_callback(WifiEventCallback cb);
    void poll_events(); // Called in event loop

    // Embedded DHCPv4 engine
    bool acquire_dhcp_lease(const std::string& interface_name);

private:
    WifiEngine();
    ~WifiEngine();

    bool load_samsung_wifi_modules();
    bool bring_interface_up(const std::string& ifname);
    bool start_wpa_supplicant_if_needed();
    std::string send_wpa_cmd(const std::string& cmd);

    bool enabled_ = false;
    std::string ifname_ = "wlan0";
    int ctrl_fd_ = -1;
    int monitor_fd_ = -1;
    WifiEventCallback event_cb_;
    WifiStatus last_status_;
};

} // namespace foxd

#endif // FOXD_WIFI_ENGINE_H
