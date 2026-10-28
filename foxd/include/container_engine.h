#ifndef FOXD_CONTAINER_ENGINE_H
#define FOXD_CONTAINER_ENGINE_H

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace foxd {

enum class StoragePersistenceMode {
    EPHEMERAL_RAM,
    PERSISTENT_DATA,
    LOOP_IMAGE
};

struct PartitionMountConfig {
    std::string source_type; // PARTITION_BY_NAME, LOGICAL_OR_PHYSICAL, HOST_BIND, DEV_PASSTHROUGH
    std::string source;      // e.g. "USERDATA", "system", "EFS", "/external_sd"
    std::string target;      // e.g. "/mnt/data", "/mnt/system"
    std::string fs_type;     // e.g. "auto", "ext4", "f2fs"
    bool writable = false;
    std::vector<std::string> options;
};

struct ContainerConfig {
    std::string container_id;
    std::string display_name;
    std::string distro;
    std::string base_image_path;

    StoragePersistenceMode persistence_mode = StoragePersistenceMode::EPHEMERAL_RAM;
    bool copy_base_sqfs_to_ram = true;
    int ram_upperdir_limit_mb = 1024;
    int zram_swap_pool_mb = 1536;
    std::string persistent_data_path;
    std::string loop_image_path;

    std::vector<PartitionMountConfig> partition_mounts;

    std::string hostname = "m20lte-linux";
    std::vector<std::string> init_command = {"/bin/sh"};
    std::vector<std::string> autostart_services;
    int ssh_port = 2222;
    std::string network_mode = "HOST";
    std::vector<std::string> dns_servers = {"1.1.1.1", "8.8.8.8"};
    std::map<std::string, std::string> environment;
};

struct ContainerStatus {
    std::string container_id;
    bool running = false;
    int pid = -1;
    long long ram_usage_bytes = 0;
    std::string ip_address;
    std::string state; // "STOPPED", "STARTING", "RUNNING", "ERROR"
};

class ContainerEngine {
public:
    static ContainerEngine& instance();

    bool initialize();

    bool create_container(const ContainerConfig& cfg);
    bool start_container(const std::string& container_id);
    bool stop_container(const std::string& container_id);
    bool destroy_container(const std::string& container_id);

    ContainerStatus get_container_status(const std::string& container_id);
    std::vector<ContainerStatus> list_containers();

    // Configuration Export / Import
    std::string export_config_json(const ContainerConfig& cfg);
    ContainerConfig import_config_json(const std::string& json_str);

private:
    ContainerEngine();
    ~ContainerEngine();

    bool setup_ram_tmpfs(const ContainerConfig& cfg, const std::string& run_dir);
    bool setup_overlayfs(const ContainerConfig& cfg, const std::string& rootfs_dir, const std::string& upper_dir, const std::string& work_dir);
    bool perform_partition_mounts(const ContainerConfig& cfg, const std::string& rootfs_dir);

    std::map<std::string, ContainerConfig> configs_;
    std::map<std::string, ContainerStatus> statuses_;
};

} // namespace foxd

#endif // FOXD_CONTAINER_ENGINE_H
