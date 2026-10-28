#include "m20lte_hal.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdint>
#include <arpa/inet.h>

namespace foxd {

M20LteHal& M20LteHal::instance() {
    static M20LteHal hal;
    return hal;
}

M20LteHal::M20LteHal() {
}

bool M20LteHal::initialize() {
    // Detect flashlight node
    const char* nodes[] = {
        "/sys/class/camera/flash/rear_flash",
        "/sys/devices/virtual/camera/flash/rear_flash",
        "/sys/class/leds/flashlight/brightness"
    };

    for (const char* n : nodes) {
        if (access(n, W_OK) == 0) {
            flashlight_node_ = n;
            break;
        }
    }
    return true;
}

PartitionTargetMode M20LteHal::detect_partition_mode() {
    // Check if SUPER partition exists in GPT
    if (access("/dev/block/platform/13500000.dwmmc0/by-name/SUPER", F_OK) == 0) {
        return PartitionTargetMode::UNIFIED_SUPER_GPT;
    }

    // Check for Retrofit Dynamic partition metadata in SYSTEM (LpMetadata magic: 0x616C4467)
    int fd = open("/dev/block/platform/13500000.dwmmc0/by-name/SYSTEM", O_RDONLY);
    if (fd >= 0) {
        uint32_t magic = 0;
        lseek(fd, 0x1000, SEEK_SET); // LpMetadata header offset
        if (read(fd, &magic, sizeof(magic)) == sizeof(magic)) {
            if (magic == 0x616C4467) {
                close(fd);
                return PartitionTargetMode::RETROFIT_DYNAMIC;
            }
        }
        close(fd);
    }

    return PartitionTargetMode::NON_DYNAMIC_PHYSICAL;
}

bool M20LteHal::switch_to_dynamic_mode() {
    // Map logical partitions via device-mapper / fastbootd
    system("dmctl create system linear /dev/block/platform/13500000.dwmmc0/by-name/SYSTEM 0 2097152 2>/dev/null");
    return true;
}

bool M20LteHal::switch_to_nondynamic_mode() {
    // Unmap device-mapper logical devices
    system("dmctl delete system 2>/dev/null; dmctl delete vendor 2>/dev/null; dmctl delete product 2>/dev/null");
    return true;
}

std::vector<std::string> M20LteHal::get_active_partitions() {
    std::vector<std::string> parts = {"boot", "recovery", "system", "vendor", "cache", "modem", "efs", "cpefs", "up_param", "dtbo", "vbmeta"};
    if (detect_partition_mode() != PartitionTargetMode::NON_DYNAMIC_PHYSICAL) {
        parts.push_back("product");
        parts.push_back("odm");
    }
    return parts;
}

FlashlightState M20LteHal::get_flashlight_state() {
    fl_state_.active_node = flashlight_node_;
    return fl_state_;
}

bool M20LteHal::set_flashlight(bool enable, int level) {
    if (level < 1) level = 1;
    if (level > 10) level = 10;

    int raw_val = 0;
    if (enable) {
        // m20lte S2MU004 flash controller fine current levels: 1001..1010
        raw_val = 1000 + level;
    }

    std::ofstream f(flashlight_node_);
    if (!f.is_open()) return false;
    f << raw_val << "\n";
    f.close();

    fl_state_.enabled = enable;
    fl_state_.level = level;
    fl_state_.raw_value = raw_val;
    return true;
}

bool M20LteHal::backup_efs_vault(const std::string& destination_dir) {
    mkdir(destination_dir.c_str(), 0755);
    std::string efs_out = destination_dir + "/efs_m20lte.img";
    std::string cpefs_out = destination_dir + "/cpefs_m20lte.img";

    std::string cmd1 = "dd if=/dev/block/platform/13500000.dwmmc0/by-name/EFS of=" + efs_out + " bs=4096";
    std::string cmd2 = "dd if=/dev/block/platform/13500000.dwmmc0/by-name/CPEFS of=" + cpefs_out + " bs=4096";

    return (system(cmd1.c_str()) == 0 && system(cmd2.c_str()) == 0);
}

bool M20LteHal::restore_efs_vault(const std::string& vault_archive_path) {
    std::string efs_in = vault_archive_path + "/efs_m20lte.img";
    std::string cpefs_in = vault_archive_path + "/cpefs_m20lte.img";

    std::string cmd1 = "dd if=" + efs_in + " of=/dev/block/platform/13500000.dwmmc0/by-name/EFS bs=4096";
    std::string cmd2 = "dd if=" + cpefs_in + " of=/dev/block/platform/13500000.dwmmc0/by-name/CPEFS bs=4096";

    system(cmd1.c_str());
    system(cmd2.c_str());
    return heal_efs_permissions();
}

bool M20LteHal::heal_efs_permissions() {
    system("chown -R 1001:1001 /efs /mnt/vendor/cpefs 2>/dev/null");
    system("restorecon -R /efs /mnt/vendor/cpefs 2>/dev/null");
    return true;
}

bool M20LteHal::disable_dm_verity_and_avb() {
    int fd = open("/dev/block/platform/13500000.dwmmc0/by-name/VBMETA", O_RDWR);
    if (fd < 0) return false;

    // Byte offset 120 holds AVB top-level flags (0x03 = HASHTREE_DISABLED | VERIFICATION_DISABLED)
    uint32_t flags = htonl(0x03);
    lseek(fd, 120, SEEK_SET);
    write(fd, &flags, sizeof(flags));
    close(fd);
    return true;
}

bool M20LteHal::disable_force_encrypt() {
    // Patches fstab in vendor if mounted
    system("sed -i 's/fileencryption=ice/encryptable=footer/g' /vendor/etc/fstab.exynos7904 2>/dev/null");
    system("sed -i 's/forceencrypt=footer/encryptable=footer/g' /vendor/etc/fstab.exynos7904 2>/dev/null");
    return true;
}

bool M20LteHal::disable_vaultkeeper_rmm() {
    system("rm -f /vendor/bin/vaultkeeperd /system/bin/vaultkeeperd 2>/dev/null");
    // Clear steady partition RMM state
    system("dd if=/dev/zero of=/dev/block/platform/13500000.dwmmc0/by-name/STEADY bs=4096 count=16 2>/dev/null");
    return true;
}

bool M20LteHal::ensure_seandroidenforce_trailer(const std::string& partition_path) {
    int fd = open(partition_path.c_str(), O_RDWR);
    if (fd < 0) return false;

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 16) { close(fd); return false; }

    char tail[16];
    lseek(fd, size - 16, SEEK_SET);
    read(fd, tail, 16);

    const char* expected = "SEANDROIDENFORCE";
    if (std::memcmp(tail, expected, 16) != 0) {
        lseek(fd, size, SEEK_SET);
        write(fd, expected, 16);
    }
    close(fd);
    return true;
}

bool M20LteHal::flash_odin_tar(const std::string& tar_path, std::string& log_output) {
    std::string cmd = "tar -tf \"" + tar_path + "\"";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return false;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        std::string fname = line;
        while (!fname.empty() && (fname.back() == '\n' || fname.back() == '\r')) fname.pop_back();

        if (fname == "recovery.img" || fname == "recovery.img.lz4") {
            log_output += "Flashing recovery payload to RECOVERY...\n";
            system(("tar -xOf \"" + tar_path + "\" " + fname + " | lz4 -d -q | dd of=/dev/block/platform/13500000.dwmmc0/by-name/RECOVERY bs=4096 2>/dev/null").c_str());
        } else if (fname == "boot.img" || fname == "boot.img.lz4") {
            log_output += "Flashing boot payload to BOOT...\n";
            system(("tar -xOf \"" + tar_path + "\" " + fname + " | lz4 -d -q | dd of=/dev/block/platform/13500000.dwmmc0/by-name/BOOT bs=4096 2>/dev/null").c_str());
        }
    }
    pclose(fp);
    return true;
}

} // namespace foxd
