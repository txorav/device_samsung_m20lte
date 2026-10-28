# Technical Specification: FoxUI System Control Suite & `m20lte` Platform Reference

**Document Version:** 3.0.0  
**Target Platform:** Samsung Galaxy M20 (`m20lte` / `SM-M205F`, `SM-M205FN`, `SM-M205G`, `SM-M205M`, `SM-M205N`) — Exynos 7904 (`universal7904`) & Generic Embedded Android/Linux Hybrid Environments  
**Execution Modes:** Userspace Privileged System Daemon (`foxd`) + Custom Recovery / Pre-boot Environment (OrangeFox / FoxUI Native DRM/FBDev)  
**Subsystems:**
1. Zero-CLI Graphical Wi-Fi Stack (`nl80211` + `wpa_ctrl` + Native DHCP/SLAAC)
2. Wireless ADB (TCP/TLS Pairing + mDNS) & Native Recovery `scrcpy` Server (`FBDev`/`DRM` + `uinput` Touch/Key Injection)
3. RAM-First Multi-Distro Linux Container Engine (Ephemeral RAM, `/data` Mount, Arbitrary `m20lte` Partition Mounting & Full Config Export)
4. Complete Unified Root Suite (Auto-Detector, Surgical Uninstallers, Offline/Online Module Installer, KPM Loader, SuList/App Profiles & Play Integrity Toolkit)
5. Full-Lifecycle App & Data Manager (Backup/Restore, Split-APK Installer, System Debloater/Uninstaller, App Freezer, Permission/Component Manager)
6. `m20lte` Dual-Target Partition Engine (Dynamic `super` & Non-Dynamic Standard Targets), S2MU004 Flashlight HAL & Custom ROM Journey Utility Suite

---

## 1. System Architecture Overview

This specification defines a unified, privileged system platform engineered specifically around the hardware constraints and custom ROM lifecycle of the Samsung Galaxy M20 (`m20lte`) while remaining portable across Android/Linux devices. Every subsystem eliminates reliance on fragile shell scripts or manual CLI wrappers (`iw`, `wpa_cli`, `ip`, `insmod`, `tar` shell pipelines) by routing low-level kernel interfaces, Netlink sockets, `wpa_supplicant` control pipes, device-mapper ioctls, Linux namespaces, framebuffer/DRM planes, `uinput` virtual devices, and sysfs nodes through a privileged C++/Rust core daemon (`foxd`) connected to a declarative **FoxUI** frontend.

```
+---------------------------------------------------------------------------------------------------+
|                                          FoxUI Frontend                                           |
|  [ Mode A: Userspace FoxUI (Svelte 5 + Tailwind 4 WebView / KernelSU WebUI / Tauri)             ] |
|  [ Mode B: Recovery FoxUI Native (OrangeFox/minui FBDev/DRM C++ XML Engine)                     ] |
|  [ Mode C: Remote PC Mirror via Native Recovery scrcpy Stream (H.264/Raw + uinput Touch/Key)    ] |
+-------------------------------------------------+-------------------------------------------------+
                                                  |
                                Unix Domain Socket IPC (/dev/socket/foxd.sock)
                                JSON-RPC 2.0 + Binary Stream Multiplexing
                                                  |
+-------------------------------------------------v-------------------------------------------------+
|                                   Privileged Core Daemon (foxd)                                   |
|          Runs as UID 0 (root) with CAP_SYS_ADMIN, CAP_NET_ADMIN, SELinux u:r:recovery:s0          |
|          or u:r:su:s0 (Userspace)                                                                 |
+----------------+----------------+----------------+----------------+---------------+---------------+
| 1. Wi-Fi Stack | 2. ADB & scrcpy| 3. Containers  | 4. Root Suite  | 5. App Suite  | 6. m20lte HAL |
| - SCSC Bringup | - Wireless ADB | - Ephemeral RAM| - Auto-Detect  | - Backup/Rest | - Dynamic/Std |
| - nl80211 Sock | - TLS Pairing  | - /data Mount  | - Uninstallers | - APK/Split   |   liblp Mapper|
| - wpa_ctrl IPC | - mDNS Bonjour | - Partition Mnt| - Module Inst  |   Installer   | - S2MU004 LED |
| - Native DHCP  | - FB0/DRM Grab | - JSON/TOML Cfg| - KPM/SuList   | - Uninstaller | - EFS/Modem   |
| - Captive Check| - uinput Hid   | - Multi-Distro | - PlayIntegrity| - Freeze/Wipe | - AVB/VaultKp |
+----------------+----------------+----------------+----------------+---------------+---------------+
```

### 1.1 Triple-Target FoxUI Presentation Layer

Because the suite operates inside a booted Android ROM, inside the custom recovery image (fitting within `m20lte`'s strict 32 MB `RECOVERY` partition budget), and remotely over Wireless/USB `scrcpy`, the UI is decoupled via a strict IPC schema:

1. **Userspace Target (`@foxui/core` - Svelte 5 + Tailwind 4):** Rendered inside a hardware-accelerated system WebView or KernelSU/APatch WebUI container, communicating with `foxd` via a local Unix socket bridge.
2. **Recovery / Framebuffer Target (`FoxUI Native` - C++ `minui` / FBDev / LVGL):** Renders directly to `/dev/graphics/fb0` (1080x2340 PLS TFT panel driven by Exynos DECON framebuffer on `m20lte`) using XML layout trees and C++ state bindings without requiring Android `system_server`.
3. **Recovery `scrcpy` Remote Target:** Streams the active recovery framebuffer (`/dev/graphics/fb0` or DRM CRTC) over ADB TCP/USB directly to an unmodified `scrcpy` client on a PC or another phone, even when the physical screen on `m20lte` is broken or unresponsive.

---

## 2. Subsystem 1: Full Graphical Wi-Fi Implementation with FoxUI (Zero-CLI)

Implementing full Wi-Fi inside both custom recovery and minimal userspace environments without CLI wrappers (`iw`, `wpa_cli`, `ip`, `ndc`) requires direct socket-level communication with the Linux kernel's `cfg80211`/`mac80211` stack, Samsung's Exynos Wi-Fi driver architecture, and `wpa_supplicant` control sockets.

### 2.1 `m20lte` Hardware Initialization & Firmware Bring-Up

In recovery mode or standalone Linux boots where Android's `WifiService` and `wificond` are inactive, `foxd` initializes the wireless hardware directly:

1. **Kernel Module & SCSC/WLAN Driver Bring-Up:**
   * Inspect `/proc/net/dev` for `wlan0`.
   * If absent, temporarily mount `/vendor` (from either `/dev/block/platform/13500000.dwmmc0/by-name/VENDOR` or `/dev/block/mapper/vendor` depending on dynamic partition detection) read-only at `/mnt/vendor_wifi`.
   * Locate the wireless driver module (`scsc_wlan.ko` / `bcmdhd.ko` or custom kernel built-in driver) and firmware blobs under `/vendor/etc/wifi/` and `/vendor/firmware/wifi/`.
   * Update `/sys/module/firmware_class/parameters/path` with `/vendor/etc/wifi:/vendor/firmware` so kernel `request_firmware()` calls resolve automatically in recovery.
   * Load kernel modules using the `finit_module(int fd, const char *param_values, int flags)` syscall.
   * Enable the wireless subsystem by writing `"1"` or `"ON"` to the platform rfkill/wlan power node (`/sys/class/rfkill/rfkill0/state` or `/dev/wmtWifi` / SCSC service trigger).

2. **Interface Activation via `RTNETLINK`:**
   * Open an `AF_NETLINK` socket with protocol `NETLINK_ROUTE`.
   * Construct and transmit an `RTM_NEWLINK` (`struct ifinfomsg`) message setting `ifi_flags |= IFF_UP` and `ifi_change |= IFF_UP` on `if_nametoindex("wlan0")`.
   * Read the hardware MAC address from `/efs/wifi/.mac.info` (Samsung EFS MAC storage on `m20lte`) and apply it via `RTM_NEWLINK` (`IFLA_ADDRESS`) if the driver boots with a zeroed or randomized fallback MAC.

### 2.2 Direct `nl80211` and `wpa_supplicant` Control Socket Architecture

Instead of spawning `wpa_cli`, `foxd` spawns `wpa_supplicant` as a supervised child daemon with `-O /run/wpa_supplicant` and links directly against `libwpa_client` (implementing the Unix datagram `wpa_ctrl` protocol) alongside `libnl-genl-3` (`nl80211`):

* **Control Socket Path:** `/run/wpa_supplicant/wlan0` (recovery) or `/data/vendor/wifi/wpa/sockets/wlan0` (userspace).
* **Dual Socket Channel:**
  * `ctrl_conn`: Synchronous request/response socket (`SCAN`, `SCAN_RESULTS`, `ADD_NETWORK`, `SET_NETWORK`, `ENABLE_NETWORK`, `SELECT_NETWORK`, `SAVE_CONFIG`, `DISCONNECT`).
  * `monitor_conn`: Asynchronous event socket attached via `wpa_ctrl_attach()` registered in `foxd`'s central `epoll` loop. Events such as `<3>CTRL-EVENT-SCAN-RESULTS`, `<3>CTRL-EVENT-CONNECTED`, `<3>CTRL-EVENT-SSID-TEMP-DISABLED reason=WRONG_KEY`, and `<3>CTRL-EVENT-DISCONNECTED` are parsed and emitted as JSON-RPC notifications to FoxUI.

---

## 3. Subsystem 2: Full Wireless ADB & Native Recovery `scrcpy` Engine

Standard Android `scrcpy` (`scrcpy-server.jar`) fails in custom recovery because recovery lacks `app_process`, Dalvik/ART, `SurfaceFlinger`, and `InputManager`. To provide **full `scrcpy` screen mirroring and remote mouse/keyboard control inside recovery** (over both USB and Wireless ADB), `foxd` implements a native C++/Rust **`fox-scrcpy-server`** alongside a full Wireless ADB daemon manager.

### 3.1 Full Wireless ADB Manager (TCP/IP + Android 11+ TLS Pairing + mDNS)

Once connected to Wi-Fi via Subsystem 1 (or USB RNDIS/NCM tether), FoxUI provides a dedicated **Wireless ADB & Remote Control** panel:

1. **Legacy TCP/IP Mode (Port `5555` or Custom):**
   * `foxd` sets `service.adb.tcp.port=5555` (via internal property service in recovery `/sbin/adbd`) and restarts `adbd` with TCP socket listener active on `0.0.0.0:5555`.
   * Displays the exact connection command (`adb connect <wlan0_ip>:5555`) prominently on the FoxUI dashboard.
2. **Android 11+ Wireless Debugging (SPAKE2+ TLS Pairing & QR Code):**
   * Embeds the `adbd` TLS pairing engine and an internal multicast DNS (`mDNS` / Bonjour) responder broadcasting `_adb-tls-pairing._tcp.local` and `_adb-tls-connect._tcp.local`.
   * **FoxUI Pairing Modes:**
     * **QR Code Scanner / Generator:** Renders an on-screen QR code in FoxUI (`WIFI:T:ADB;S:<service_name>;P:<6_digit_pin>;;`) that Android Studio or `adb-wifi` tools can scan, OR displays a **6-Digit Pairing Code + Pairing Port** directly in FoxUI.
     * **RSA Key Vault Synchronization:** Synchronizes authorized public keys between `/data/misc/adb/adb_keys` and recovery's `/adb_keys` + `/sdcard/FoxUI/adb_keys.pub` so authorized PCs never get `unauthorized` prompts in recovery even when `/data` is wiped or encrypted.

### 3.2 Native Recovery `scrcpy` Server (`fox-scrcpy-server`)

When the user runs `scrcpy` (or `scrcpy --tcpip=<wlan0_ip>:5555`) on their PC while `m20lte` is in FoxUI Recovery, `scrcpy` pushes `/data/local/tmp/scrcpy-server.jar` and executes `CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server <version> <args...>`.

In FoxUI Recovery, `/sbin/app_process` is a native shim binary wired directly into `foxd`'s **Native Recovery `scrcpy` Engine**:

```
+---------------------------------------------------------------------------------------------------+
|                        Native Recovery scrcpy Pipeline on m20lte (Zero-Java)                      |
+-------------------------------------------------+-------------------------------------------------+
                                                  |
         PC runs `scrcpy` over USB or Wireless ADB (`adb connect 192.168.1.42:5555`)
                                                  |
                                                  v
+---------------------------------------------------------------------------------------------------+
| 1. /sbin/app_process Shim Intercepts `com.genymobile.scrcpy.Server` Invocation                    |
|    - Parses scrcpy protocol version (v2.x / v3.x), `scid`, `max_size`, `max_fps`, `raw_stream`    |
|    - Opens reverse/forward local abstract socket `localabstract:scrcpy_<scid>`                    |
|    - Sends 1-byte dummy header + 64-byte device name ("Samsung SM-M205F [FoxUI Recovery]")        |
+-------------------------------------------------+-------------------------------------------------+
                                                  |
        +-----------------------------------------+-----------------------------------------+
        | Video Socket Stream (Device -> PC)      | Control Socket Stream (PC -> Device)    |
        v                                         v                                         |
+-------+---------------------------------+-------+-----------------------------------------+-------+
| 2. Exynos 7904 Framebuffer Capture      | 3. Kernel `uinput` HID Touch & Key Injection            |
| - Maps `/dev/graphics/fb0` (1080x2340   | - Opens `/dev/uinput` and creates two virtual devices:  |
|   BGRA8888) via `mmap` + damage tracker |   a) `foxui-scrcpy-touch` (Multi-Touch Protocol B,      |
| - Hardware/NEON Encoder Path:           |      `ABS_MT_POSITION_X` 0..1079, `Y` 0..2339)          |
|   a) Exynos MFC (`/dev/video7` H.264    |   b) `foxui-scrcpy-kbd` (`EV_KEY` full keyboard +       |
|      hardware encoder if V4L2 active)   |      `KEY_POWER`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`)     |
|   b) Ultra-fast ARMv8 NEON `openh264` / | - Translates scrcpy `INJECT_TOUCH_EVENT`,               |
|      `x264` (`ultrafast`, `zerolatency`)|   `INJECT_KEYCODE`, `INJECT_TEXT`, `INJECT_SCROLL`,     |
|      60 FPS @ 540x1170 or 1080x2340     |   and `SET_CLIPBOARD` into immediate `uinput` events    |
+-----------------------------------------+---------------------------------------------------------+
```

---

## 4. Subsystem 3: RAM-First Multi-Distro Linux Container Engine

Designed for `m20lte`'s 3 GB or 4 GB LPDDR4X RAM configuration, `foxd` includes a zero-footprint Linux container runtime supporting **Pure Ephemeral RAM Mode**, **Persistent `/data` Mode**, and **Arbitrary Partition Mounting**:

- **Ephemeral RAM Mode:** Zero persistent disk writes, zram-backed tmpfs upperdir, instant self-destruct on stop.
- **Persistent /data Mode:** Native eMMC speeds without static disk image limits.
- **Partition Mounter:** Direct attachment of `/data`, `/system`, `/vendor`, `/efs`, `/cpefs`, `/cache`, and raw `/dev/block/` devices with configurable Read-Only / Read-Write flags.
- **Multi-Distro Catalog:** Alpine, Debian, Arch Linux ARM, Kali Minimal, Ubuntu, Void, Fedora.
- **Exportable Schema:** Full JSON/TOML configuration import and export (`.foxcontainer.json`).

---

## 5. Subsystem 4: Complete Unified Root Suite

- **Auto-Detector:** Real-time inspection of `BOOT`, kernel image symbols, and `/data` to identify Magisk, Kitsune Mask, KernelSU, KernelSU-Next, and APatch.
- **Surgical Uninstallers:** Native unpatching without external ZIP dependencies.
- **Module Manager:** Full installation, toggle, remove, and property/SELinux rule editor in Userspace and Recovery.
- **KPM Loader:** APatch `.kpm` runtime manager.
- **SuList & App Profiles:** Direct configuration of `magisk.db` (DenyList/SuList) and KernelSU `allowlist`.
- **Play Integrity & Keybox Toolkit:** Fingerprint updater (`pif.json`) and TrickyStore `keybox.xml` manager.

---

## 6. Subsystem 5: Full-Lifecycle App & Data Manager

- **App & Split-APK Installer / Uninstaller:** Direct package management across user and system apps.
- **OneUI Debloater:** Curated debloat presets for `m20lte`.
- **App Freezer:** Suspend and disable components offline and online.
- **Cross-ROM Multi-Stream Backup (`.foxbackup`):** 5-stage restore pipeline with dynamic UID/GID rewriting, cache GID recalculation, SELinux context restoration, and SSAID preservation.

---

## 7. Subsystem 6: Samsung Galaxy M20 Hardware & Partition Engine

- **Dual-Target Partition Resolver:** Dynamic Super (Retrofit & GPT Unified) vs Non-Dynamic Standard with automatic pre-flash detection and clean mode switching.
- **S2MU004 Camera Flashlight HAL:** Direct hardware control at `/sys/class/camera/flash/rear_flash` with current levels `1001`–`1010` and thermal safeguards.
- **EFS / CPEFS Golden Vault:** Automatic first-boot backup and NV-data permissions healer.
- **Samsung Security Patchers:** DM-verity/AVB 2.0 flags, ForceEncrypt disabler, VaultKeeper/RMM neutralizer, Defex bypass, and `SEANDROIDENFORCE` trailer verification.
- **Native Odin TAR / LZ4 Flasher:** On-device flashing of Samsung Odin archives and LZ4-compressed images.
