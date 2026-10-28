#ifndef FOXD_SCRCPY_SERVER_H
#define FOXD_SCRCPY_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>

namespace foxd {

struct ScrcpyConfig {
    int port = 5555;
    int max_width = 1080;
    int max_height = 2340;
    int bit_rate = 8000000;
    int max_fps = 60;
    bool autostart_on_boot = false;
};

class ScrcpyServer {
public:
    static ScrcpyServer& instance();

    bool initialize(const ScrcpyConfig& config = ScrcpyConfig());
    bool start_server();
    bool stop_server();
    bool is_running() const;

    // Wireless ADB management
    bool enable_wireless_adb(int port = 5555);
    bool disable_wireless_adb();
    std::string get_adb_connect_address() const;

    // Direct Framebuffer & uinput helpers
    bool setup_uinput_touch();
    bool setup_uinput_keyboard();
    void inject_touch_event(int action, int pointer_id, int x, int y, float pressure);
    void inject_key_event(int keycode, int action);

private:
    ScrcpyServer();
    ~ScrcpyServer();

    void server_worker();
    void stream_framebuffer_loop(int client_fd);
    void handle_control_loop(int client_fd);

    ScrcpyConfig config_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    int server_socket_ = -1;
    int fb_fd_ = -1;
    int uinput_touch_fd_ = -1;
    int uinput_kbd_fd_ = -1;

    void* fb_mmap_ptr_ = nullptr;
    size_t fb_size_ = 0;
    uint32_t fb_width_ = 1080;
    uint32_t fb_height_ = 2340;
    uint32_t fb_bpp_ = 32;
};

} // namespace foxd

#endif // FOXD_SCRCPY_SERVER_H
