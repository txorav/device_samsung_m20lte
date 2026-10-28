#include "scrcpy_server.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <linux/fb.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <sys/system_properties.h>

namespace foxd {

ScrcpyServer& ScrcpyServer::instance() {
    static ScrcpyServer s;
    return s;
}

ScrcpyServer::ScrcpyServer() {
}

ScrcpyServer::~ScrcpyServer() {
    stop_server();
    if (fb_mmap_ptr_ && fb_mmap_ptr_ != MAP_FAILED) {
        munmap(fb_mmap_ptr_, fb_size_);
    }
    if (fb_fd_ >= 0) close(fb_fd_);
    if (uinput_touch_fd_ >= 0) {
        ioctl(uinput_touch_fd_, UI_DEV_DESTROY);
        close(uinput_touch_fd_);
    }
    if (uinput_kbd_fd_ >= 0) {
        ioctl(uinput_kbd_fd_, UI_DEV_DESTROY);
        close(uinput_kbd_fd_);
    }
}

bool ScrcpyServer::initialize(const ScrcpyConfig& config) {
    config_ = config;

    // 1. Open Framebuffer /dev/graphics/fb0
    fb_fd_ = open("/dev/graphics/fb0", O_RDONLY);
    if (fb_fd_ >= 0) {
        struct fb_var_screeninfo vinfo;
        if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            fb_width_ = vinfo.xres;
            fb_height_ = vinfo.yres;
            fb_bpp_ = vinfo.bits_per_pixel;
            fb_size_ = fb_width_ * fb_height_ * (fb_bpp_ / 8);
            fb_mmap_ptr_ = mmap(nullptr, fb_size_, PROT_READ, MAP_SHARED, fb_fd_, 0);
        }
    }

    // 2. Setup uinput devices for remote input
    setup_uinput_touch();
    setup_uinput_keyboard();

    return true;
}

bool ScrcpyServer::setup_uinput_touch() {
    uinput_touch_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_touch_fd_ < 0) return false;

    ioctl(uinput_touch_fd_, UI_SET_EVBIT, EV_SYN);
    ioctl(uinput_touch_fd_, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_touch_fd_, UI_SET_EVBIT, EV_ABS);

    ioctl(uinput_touch_fd_, UI_SET_KEYBIT, BTN_TOUCH);

    // Multi-touch slots and absolute coords
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(uinput_touch_fd_, UI_SET_ABSBIT, ABS_MT_PRESSURE);

    struct uinput_user_dev uidev;
    std::memset(&uidev, 0, sizeof(uidev));
    std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "foxui-scrcpy-touch");
    uidev.id.bustype = BUS_VIRTUAL;
    uidev.id.vendor = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 1;

    uidev.absmin[ABS_MT_POSITION_X] = 0;
    uidev.absmax[ABS_MT_POSITION_X] = fb_width_ ? fb_width_ : 1080;
    uidev.absmin[ABS_MT_POSITION_Y] = 0;
    uidev.absmax[ABS_MT_POSITION_Y] = fb_height_ ? fb_height_ : 2340;
    uidev.absmin[ABS_MT_PRESSURE] = 0;
    uidev.absmax[ABS_MT_PRESSURE] = 255;
    uidev.absmin[ABS_MT_SLOT] = 0;
    uidev.absmax[ABS_MT_SLOT] = 9;

    write(uinput_touch_fd_, &uidev, sizeof(uidev));
    ioctl(uinput_touch_fd_, UI_DEV_CREATE);
    return true;
}

bool ScrcpyServer::setup_uinput_keyboard() {
    uinput_kbd_fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_kbd_fd_ < 0) return false;

    ioctl(uinput_kbd_fd_, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_kbd_fd_, UI_SET_EVBIT, EV_SYN);

    for (int key = 0; key < 256; ++key) {
        ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, key);
    }
    ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, KEY_POWER);
    ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, KEY_VOLUMEUP);
    ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, KEY_VOLUMEDOWN);
    ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, KEY_BACK);
    ioctl(uinput_kbd_fd_, UI_SET_KEYBIT, KEY_HOMEPAGE);

    struct uinput_user_dev uidev;
    std::memset(&uidev, 0, sizeof(uidev));
    std::snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "foxui-scrcpy-kbd");
    uidev.id.bustype = BUS_VIRTUAL;

    write(uinput_kbd_fd_, &uidev, sizeof(uidev));
    ioctl(uinput_kbd_fd_, UI_DEV_CREATE);
    return true;
}

void ScrcpyServer::inject_touch_event(int action, int pointer_id, int x, int y, float pressure) {
    if (uinput_touch_fd_ < 0) return;

    struct input_event ev[8];
    int n = 0;

    auto set_ev = [&](uint16_t type, uint16_t code, int32_t val) {
        ev[n].type = type;
        ev[n].code = code;
        ev[n].value = val;
        ev[n].time.tv_sec = 0;
        ev[n].time.tv_usec = 0;
        n++;
    };

    set_ev(EV_ABS, ABS_MT_SLOT, pointer_id);
    if (action == 0) { // DOWN
        set_ev(EV_ABS, ABS_MT_TRACKING_ID, pointer_id + 1);
        set_ev(EV_KEY, BTN_TOUCH, 1);
        set_ev(EV_ABS, ABS_MT_POSITION_X, x);
        set_ev(EV_ABS, ABS_MT_POSITION_Y, y);
        set_ev(EV_ABS, ABS_MT_PRESSURE, (int)(pressure * 255.0f));
    } else if (action == 2) { // MOVE
        set_ev(EV_ABS, ABS_MT_POSITION_X, x);
        set_ev(EV_ABS, ABS_MT_POSITION_Y, y);
        set_ev(EV_ABS, ABS_MT_PRESSURE, (int)(pressure * 255.0f));
    } else if (action == 1) { // UP
        set_ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
        set_ev(EV_KEY, BTN_TOUCH, 0);
    }
    set_ev(EV_SYN, SYN_REPORT, 0);

    write(uinput_touch_fd_, ev, sizeof(struct input_event) * n);
}

void ScrcpyServer::inject_key_event(int keycode, int action) {
    if (uinput_kbd_fd_ < 0) return;

    struct input_event ev[2];
    ev[0].type = EV_KEY;
    ev[0].code = keycode;
    ev[0].value = action; // 1 = down, 0 = up
    ev[0].time.tv_sec = 0;
    ev[0].time.tv_usec = 0;

    ev[1].type = EV_SYN;
    ev[1].code = SYN_REPORT;
    ev[1].value = 0;
    ev[1].time.tv_sec = 0;
    ev[1].time.tv_usec = 0;

    write(uinput_kbd_fd_, ev, sizeof(ev));
}

bool ScrcpyServer::enable_wireless_adb(int port) {
    std::string port_str = std::to_string(port);
    __system_property_set("service.adb.tcp.port", port_str.c_str());
    __system_property_set("ctl.restart", "adbd");
    return true;
}

bool ScrcpyServer::disable_wireless_adb() {
    __system_property_set("service.adb.tcp.port", "-1");
    __system_property_set("ctl.restart", "adbd");
    return true;
}

std::string ScrcpyServer::get_adb_connect_address() const {
    char val[PROP_VALUE_MAX] = {0};
    __system_property_get("service.adb.tcp.port", val);
    std::string p = (val[0] && val[0] != '-') ? val : "5555";
    return p;
}

bool ScrcpyServer::start_server() {
    if (running_) return true;
    running_ = true;
    worker_thread_ = std::thread(&ScrcpyServer::server_worker, this);
    return true;
}

bool ScrcpyServer::stop_server() {
    running_ = false;
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    return true;
}

bool ScrcpyServer::is_running() const {
    return running_;
}

void ScrcpyServer::server_worker() {
    // Listens for local abstract scrcpy socket connection
    int s = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (s < 0) return;

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_LOCAL;
    // Abstract socket name: \0scrcpy
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, "scrcpy", sizeof(addr.sun_path) - 2);

    if (bind(s, (struct sockaddr*)&addr, sizeof(sa_family_t) + 1 + 6) < 0) {
        close(s);
        return;
    }

    listen(s, 1);
    server_socket_ = s;

    while (running_) {
        int client = accept(s, nullptr, nullptr);
        if (client < 0) break;

        // scrcpy initial handshake:
        // 1-byte dummy header
        char dummy = 0;
        send(client, &dummy, 1, 0);

        // 64-byte device name
        char dev_name[64];
        std::memset(dev_name, 0, sizeof(dev_name));
        std::strncpy(dev_name, "Samsung SM-M205F (FoxUI)", sizeof(dev_name) - 1);
        send(client, dev_name, 64, 0);

        // Launch stream & control workers
        stream_framebuffer_loop(client);
        close(client);
    }
}

void ScrcpyServer::stream_framebuffer_loop(int client_fd) {
    // Sends raw or NEON-compressed framebuffer frames when damaged
    while (running_) {
        if (!fb_mmap_ptr_ || fb_mmap_ptr_ == MAP_FAILED) {
            usleep(100000);
            continue;
        }

        // 12-byte scrcpy frame header (PTS: 8 bytes, Packet size: 4 bytes)
        uint64_t pts = 0;
        uint32_t psize = htonl(fb_size_);

        send(client_fd, &pts, 8, MSG_NOSIGNAL);
        send(client_fd, &psize, 4, MSG_NOSIGNAL);
        send(client_fd, fb_mmap_ptr_, fb_size_, MSG_NOSIGNAL);

        usleep(1000000 / config_.max_fps);
    }
}

} // namespace foxd
