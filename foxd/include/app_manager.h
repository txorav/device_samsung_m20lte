#ifndef FOXD_APP_MANAGER_H
#define FOXD_APP_MANAGER_H

#include <string>
#include <vector>

namespace foxd {

struct AppInfo {
    std::string package_name;
    std::string app_name;
    std::string version_name;
    int version_code = 0;
    int uid = 0;
    bool is_system = false;
    bool is_enabled = true;
    bool is_suspended = false;
    long long data_size_bytes = 0;
    std::string apk_path;
};

struct AppBackupConfig {
    std::string package_name;
    std::string destination_dir; // e.g. "/sdcard/FoxUI/AppBackups"
    bool include_apk = true;
    bool include_data_ce = true;
    bool include_data_de = true;
    bool include_cache = false;
    bool include_external_data = false;
};

class AppManager {
public:
    static AppManager& instance();

    std::vector<AppInfo> list_installed_apps(bool include_system = true);
    AppInfo get_app_details(const std::string& package_name);

    // Lifecycle Actions
    bool install_apk(const std::string& apk_path);
    bool uninstall_app(const std::string& package_name, bool keep_data = false);
    bool freeze_app(const std::string& package_name, bool freeze);
    bool clear_app_data(const std::string& package_name);
    bool clear_app_cache(const std::string& package_name);

    // Cross-ROM Backup & Restore (.foxbackup)
    bool backup_app(const AppBackupConfig& cfg, std::string& output_archive_path);
    bool restore_app(const std::string& archive_path);

    // System Debloater
    bool debloat_preset(const std::string& preset_json_path, std::vector<std::string>& debloated_packages);

private:
    AppManager() = default;
    ~AppManager() = default;

    int parse_app_uid(const std::string& package_name);
};

} // namespace foxd

#endif // FOXD_APP_MANAGER_H
