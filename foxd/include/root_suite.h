#ifndef FOXD_ROOT_SUITE_H
#define FOXD_ROOT_SUITE_H

#include <string>
#include <vector>

namespace foxd {

struct DetectedRootInfo {
    std::string type;         // "Magisk", "KitsuneMask", "KernelSU", "APatch", "None"
    std::string mode;         // "BOOT_RAMDISK", "LKM_RAMDISK", "BUILTIN_KERNEL", "RESIDUAL_DATA_ONLY"
    std::string version;
    bool active = false;
    bool uninstaller_available = false;
    int module_count = 0;
    bool denylist_enabled = false;
    bool zygisk_enabled = false;
};

struct RootModuleInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    bool enabled = true;
    bool remove_on_reboot = false;
    bool has_webui = false;
    bool has_action_sh = false;
};

class RootSuite {
public:
    static RootSuite& instance();

    DetectedRootInfo detect_root_solution();

    // Surgical Uninstallers
    bool uninstall_magisk();
    bool uninstall_kernelsu();
    bool uninstall_apatch();
    bool clean_residual_data();

    // Module Management
    std::vector<RootModuleInfo> list_modules();
    bool toggle_module(const std::string& module_id, bool enable);
    bool remove_module(const std::string& module_id);
    bool install_module_zip(const std::string& zip_path, std::string& log_output);

    // SuList / DenyList / Play Integrity
    bool set_zygisk_enabled(bool enabled);
    bool set_denylist_enabled(bool enabled);
    bool update_play_integrity_pif(const std::string& pif_json_content);
    bool import_trickystore_keybox(const std::string& keybox_xml_content);

private:
    RootSuite() = default;
    ~RootSuite() = default;

    std::string read_file_string(const std::string& path);
    bool write_file_string(const std::string& path, const std::string& content);
};

} // namespace foxd

#endif // FOXD_ROOT_SUITE_H
