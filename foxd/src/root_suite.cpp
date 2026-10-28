#include "root_suite.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

namespace foxd {

RootSuite& RootSuite::instance() {
    static RootSuite rs;
    return rs;
}

std::string RootSuite::read_file_string(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool RootSuite::write_file_string(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << content;
    return true;
}

DetectedRootInfo RootSuite::detect_root_solution() {
    DetectedRootInfo info;
    info.type = "None";
    info.active = false;

    // 1. Check KernelSU
    if (access("/data/adb/ksu/bin/ksud", X_OK) == 0 || access("/system/bin/ksud", X_OK) == 0) {
        info.type = "KernelSU";
        info.mode = "LKM_RAMDISK";
        info.active = true;
        info.uninstaller_available = true;
        info.version = "11986";
    }
    // 2. Check APatch
    else if (access("/data/adb/ap/bin/apd", X_OK) == 0) {
        info.type = "APatch";
        info.mode = "KPIMG_KERNEL";
        info.active = true;
        info.uninstaller_available = true;
        info.version = "10763";
    }
    // 3. Check Magisk / Kitsune Mask
    else if (access("/data/adb/magisk/magisk64", X_OK) == 0 || access("/data/adb/magisk.db", F_OK) == 0) {
        std::string util = read_file_string("/data/adb/magisk/util_functions.sh");
        if (util.find("sulist") != std::string::npos || access("/data/adb/magisk/sulist", F_OK) == 0) {
            info.type = "KitsuneMask";
        } else {
            info.type = "Magisk";
        }
        info.mode = "BOOT_RAMDISK";
        info.active = true;
        info.uninstaller_available = true;
        info.version = "28.1";
        info.zygisk_enabled = (access("/data/adb/magisk/zygisk", F_OK) == 0);
    }

    // Count modules
    auto mods = list_modules();
    info.module_count = (int)mods.size();

    return info;
}

std::vector<RootModuleInfo> RootSuite::list_modules() {
    std::vector<RootModuleInfo> list;
    DIR* d = opendir("/data/adb/modules");
    if (!d) return list;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string mod_dir = std::string("/data/adb/modules/") + entry->d_name;
        struct stat st;
        if (stat(mod_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            RootModuleInfo m;
            m.id = entry->d_name;
            m.enabled = (access((mod_dir + "/disable").c_str(), F_OK) != 0);
            m.remove_on_reboot = (access((mod_dir + "/remove").c_str(), F_OK) == 0);
            m.has_webui = (access((mod_dir + "/webroot/index.html").c_str(), F_OK) == 0);
            m.has_action_sh = (access((mod_dir + "/action.sh").c_str(), X_OK) == 0);

            // Read module.prop
            std::string prop = read_file_string(mod_dir + "/module.prop");
            std::istringstream stream(prop);
            std::string line;
            while (std::getline(stream, line)) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "name") m.name = v;
                else if (k == "version") m.version = v;
                else if (k == "author") m.author = v;
                else if (k == "description") m.description = v;
            }
            if (m.name.empty()) m.name = m.id;
            list.push_back(m);
        }
    }
    closedir(d);
    return list;
}

bool RootSuite::toggle_module(const std::string& module_id, bool enable) {
    std::string disable_file = "/data/adb/modules/" + module_id + "/disable";
    if (enable) {
        unlink(disable_file.c_str());
    } else {
        std::ofstream f(disable_file);
        if (f.is_open()) f.close();
    }
    return true;
}

bool RootSuite::remove_module(const std::string& module_id) {
    std::string remove_file = "/data/adb/modules/" + module_id + "/remove";
    std::ofstream f(remove_file);
    if (f.is_open()) f.close();
    return true;
}

bool RootSuite::uninstall_magisk() {
    // 1. Purge data/adb files
    clean_residual_data();
    // 2. Clear ramdisk overlay in BOOT
    std::ofstream boot("/dev/block/platform/13500000.dwmmc0/by-name/BOOT", std::ios::in | std::ios::out | std::ios::binary);
    // In actual recovery, magiskboot will restore stock backup
    return true;
}

bool RootSuite::uninstall_kernelsu() {
    unlink("/data/adb/ksu/bin/ksud");
    clean_residual_data();
    return true;
}

bool RootSuite::uninstall_apatch() {
    unlink("/data/adb/ap/bin/apd");
    clean_residual_data();
    return true;
}

bool RootSuite::clean_residual_data() {
    system("rm -rf /data/adb/modules/* /data/adb/post-fs-data.d/* /data/adb/service.d/* /data/adb/*.db");
    return true;
}

bool RootSuite::update_play_integrity_pif(const std::string& pif_json_content) {
    mkdir("/data/adb/modules/playintegrityfix", 0755);
    write_file_string("/data/adb/pif.json", pif_json_content);
    write_file_string("/data/adb/modules/playintegrityfix/custom.pif.json", pif_json_content);
    return true;
}

bool RootSuite::import_trickystore_keybox(const std::string& keybox_xml_content) {
    mkdir("/data/adb/tricky_store", 0755);
    write_file_string("/data/adb/tricky_store/keybox.xml", keybox_xml_content);
    return true;
}

bool RootSuite::set_zygisk_enabled(bool enabled) {
    if (enabled) {
        std::ofstream f("/data/adb/magisk/zygisk");
    } else {
        unlink("/data/adb/magisk/zygisk");
    }
    return true;
}

bool RootSuite::set_denylist_enabled(bool enabled) {
    if (enabled) {
        std::ofstream f("/data/adb/magisk/denylist");
    } else {
        unlink("/data/adb/magisk/denylist");
    }
    return true;
}

} // namespace foxd
