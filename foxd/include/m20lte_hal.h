#ifndef FOXD_M20LTE_HAL_H
#define FOXD_M20LTE_HAL_H

#include <string>
#include <vector>

namespace foxd {

enum class PartitionTargetMode {
    NON_DYNAMIC_PHYSICAL,
    RETROFIT_DYNAMIC,
    UNIFIED_SUPER_GPT
};

struct FlashlightState {
    bool enabled = false;
    int level = 5;      // 1 to 10
    int raw_value = 1005; // 1001 to 1010
    std::string active_node;
};

class M20LteHal {
public:
    static M20LteHal& instance();

    bool initialize();

    // 1. Dual-Target Partition Engine
    PartitionTargetMode detect_partition_mode();
    bool switch_to_dynamic_mode();
    bool switch_to_nondynamic_mode();
    std::vector<std::string> get_active_partitions();

    // 2. S2MU004 Hardware Flashlight Controller
    FlashlightState get_flashlight_state();
    bool set_flashlight(bool enable, int level = 5);

    // 3. EFS / CPEFS Golden Vault
    bool backup_efs_vault(const std::string& destination_dir = "/sdcard/FoxUI/EFS_Vault");
    bool restore_efs_vault(const std::string& vault_archive_path);
    bool heal_efs_permissions();

    // 4. Samsung Security & Verification Patchers
    bool disable_dm_verity_and_avb();
    bool disable_force_encrypt();
    bool disable_vaultkeeper_rmm();
    bool ensure_seandroidenforce_trailer(const std::string& partition_path);

    // 5. Odin TAR / LZ4 Flasher
    bool flash_odin_tar(const std::string& tar_path, std::string& log_output);

private:
    M20LteHal();
    ~M20LteHal() = default;

    std::string flashlight_node_ = "/sys/class/camera/flash/rear_flash";
    FlashlightState fl_state_;
};

} // namespace foxd

#endif // FOXD_M20LTE_HAL_H
