#include "app_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

namespace foxd {

AppManager& AppManager::instance() {
    static AppManager am;
    return am;
}

int AppManager::parse_app_uid(const std::string& package_name) {
    std::ifstream f("/data/system/packages.list");
    if (!f.is_open()) return -1;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string pkg;
        int uid = -1;
        if (ss >> pkg >> uid) {
            if (pkg == package_name) return uid;
        }
    }
    return -1;
}

std::vector<AppInfo> AppManager::list_installed_apps(bool include_system) {
    std::vector<AppInfo> list;
    std::ifstream f("/data/system/packages.list");
    if (!f.is_open()) return list;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string pkg, seinfo, code_path;
        int uid = 0, debug_flag = 0;
        if (ss >> pkg >> uid >> debug_flag >> code_path >> seinfo) {
            AppInfo app;
            app.package_name = pkg;
            app.uid = uid;
            app.apk_path = code_path;
            app.is_system = (code_path.find("/system/") == 0 ||
                             code_path.find("/vendor/") == 0 ||
                             code_path.find("/product/") == 0 ||
                             code_path.find("/odm/") == 0);
            if (!include_system && app.is_system) continue;
            app.app_name = pkg;
            app.is_enabled = true;
            list.push_back(app);
        }
    }
    return list;
}

AppInfo AppManager::get_app_details(const std::string& package_name) {
    AppInfo app;
    app.package_name = package_name;
    app.uid = parse_app_uid(package_name);
    return app;
}

bool AppManager::install_apk(const std::string& apk_path) {
    std::string cmd = "pm install -r -d -g \"" + apk_path + "\"";
    return (system(cmd.c_str()) == 0);
}

bool AppManager::uninstall_app(const std::string& package_name, bool keep_data) {
    std::string cmd = "pm uninstall " + (keep_data ? std::string("-k ") : "") + package_name;
    return (system(cmd.c_str()) == 0);
}

bool AppManager::freeze_app(const std::string& package_name, bool freeze) {
    if (access("/system/bin/pm", X_OK) == 0) {
        std::string cmd = "pm " + (freeze ? std::string("disable-user --user 0 ") : std::string("enable ")) + package_name;
        if (system(cmd.c_str()) == 0) return true;
    }
    const char* search_dirs[] = {
        "/system/app", "/system/priv-app",
        "/vendor/app", "/product/app", "/product/priv-app"
    };
    for (const char* dir : search_dirs) {
        std::string target = std::string(dir) + "/" + package_name;
        if (freeze && access(target.c_str(), F_OK) == 0) {
            rename(target.c_str(), (target + ".disabled").c_str());
            return true;
        } else if (!freeze && access((target + ".disabled").c_str(), F_OK) == 0) {
            rename((target + ".disabled").c_str(), target.c_str());
            return true;
        }
    }
    return false;
}

bool AppManager::clear_app_data(const std::string& package_name) {
    std::string cmd = "pm clear " + package_name;
    return (system(cmd.c_str()) == 0);
}

bool AppManager::clear_app_cache(const std::string& package_name) {
    std::string cmd = "rm -rf /data/data/" + package_name + "/cache/* /data/data/" + package_name + "/code_cache/*";
    return (system(cmd.c_str()) == 0);
}

bool AppManager::backup_app(const AppBackupConfig& cfg, std::string& output_archive_path) {
    std::string dest = cfg.destination_dir.empty() ? "/sdcard/FoxUI/AppBackups" : cfg.destination_dir;
    mkdir(dest.c_str(), 0755);

    std::string out_dir = dest + "/" + cfg.package_name;
    mkdir(out_dir.c_str(), 0755);

    // Backup CE data
    if (cfg.include_data_ce) {
        std::string cmd = "tar -C /data/data/" + cfg.package_name + " -czf " + out_dir + "/data_ce.tar.gz .";
        system(cmd.c_str());
    }

    // Backup DE data
    if (cfg.include_data_de) {
        std::string de_path = "/data/user_de/0/" + cfg.package_name;
        if (access(de_path.c_str(), F_OK) == 0) {
            std::string cmd = "tar -C " + de_path + " -czf " + out_dir + "/data_de.tar.gz .";
            system(cmd.c_str());
        }
    }

    output_archive_path = out_dir;
    return true;
}

bool AppManager::restore_app(const std::string& archive_path) {
    // 5-stage restore pipeline
    std::string pkg = archive_path.substr(archive_path.find_last_of('/') + 1);
    int new_uid = parse_app_uid(pkg);
    if (new_uid < 0) new_uid = 10000;

    std::string data_path = "/data/data/" + pkg;
    mkdir(data_path.c_str(), 0755);

    // Extract CE data
    std::string ce_archive = archive_path + "/data_ce.tar.gz";
    if (access(ce_archive.c_str(), F_OK) == 0) {
        std::string cmd = "tar -xzf " + ce_archive + " -C " + data_path;
        system(cmd.c_str());
    }

    // Chown to new UID
    std::string chown_cmd = "chown -R " + std::to_string(new_uid) + ":" + std::to_string(new_uid) + " " + data_path;
    system(chown_cmd.c_str());

    // Restorecon SELinux context
    std::string restorecon_cmd = "restorecon -R " + data_path;
    system(restorecon_cmd.c_str());

    return true;
}

bool AppManager::debloat_preset(const std::string& preset_json_path, std::vector<std::string>& debloated) {
    std::ifstream f(preset_json_path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"package\":") != std::string::npos) {
            auto q1 = line.find('"', line.find(':'));
            auto q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string pkg = line.substr(q1 + 1, q2 - q1 - 1);
                freeze_app(pkg, true);
                debloated.push_back(pkg);
            }
        }
    }
    return true;
}

} // namespace foxd
