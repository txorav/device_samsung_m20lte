#include "container_engine.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sched.h>
#include <signal.h>

namespace foxd {

ContainerEngine& ContainerEngine::instance() {
    static ContainerEngine ce;
    return ce;
}

ContainerEngine::ContainerEngine() {
}

ContainerEngine::~ContainerEngine() {
}

bool ContainerEngine::initialize() {
    mkdir("/run/fox_containers", 0755);
    mkdir("/data/fox_containers", 0755);
    return true;
}

bool ContainerEngine::create_container(const ContainerConfig& cfg) {
    configs_[cfg.container_id] = cfg;
    ContainerStatus st;
    st.container_id = cfg.container_id;
    st.state = "STOPPED";
    statuses_[cfg.container_id] = st;
    return true;
}

bool ContainerEngine::setup_ram_tmpfs(const ContainerConfig& cfg, const std::string& run_dir) {
    std::string opt = "size=" + std::to_string(cfg.ram_upperdir_limit_mb) + "M,mode=0755";
    if (mount("tmpfs", run_dir.c_str(), "tmpfs", MS_NODEV, opt.c_str()) < 0) {
        return false;
    }
    mkdir((run_dir + "/upper").c_str(), 0755);
    mkdir((run_dir + "/work").c_str(), 0755);
    mkdir((run_dir + "/merged").c_str(), 0755);
    return true;
}

bool ContainerEngine::setup_overlayfs(const ContainerConfig& cfg, const std::string& rootfs_dir,
                                      const std::string& upper_dir, const std::string& work_dir) {
    std::string opts = "lowerdir=" + cfg.base_image_path + ",upperdir=" + upper_dir + ",workdir=" + work_dir;
    return (mount("overlay", rootfs_dir.c_str(), "overlay", 0, opts.c_str()) == 0);
}

bool ContainerEngine::perform_partition_mounts(const ContainerConfig& cfg, const std::string& rootfs_dir) {
    for (const auto& pm : cfg.partition_mounts) {
        std::string target = rootfs_dir + pm.target;
        mkdir(target.c_str(), 0755);

        std::string dev_path;
        if (pm.source_type == "PARTITION_BY_NAME") {
            dev_path = "/dev/block/platform/13500000.dwmmc0/by-name/" + pm.source;
        } else if (pm.source_type == "LOGICAL_OR_PHYSICAL") {
            // Check dynamic partition mapper first, fallback to physical
            std::string mapper = "/dev/block/mapper/" + pm.source;
            if (access(mapper.c_str(), F_OK) == 0) {
                dev_path = mapper;
            } else {
                dev_path = "/dev/block/platform/13500000.dwmmc0/by-name/" + pm.source;
            }
        } else if (pm.source_type == "HOST_BIND" || pm.source_type == "DEV_PASSTHROUGH") {
            dev_path = pm.source;
        }

        if (!dev_path.empty()) {
            unsigned long flags = pm.writable ? 0 : MS_RDONLY;
            if (pm.source_type == "HOST_BIND" || pm.source_type == "DEV_PASSTHROUGH") {
                mount(dev_path.c_str(), target.c_str(), nullptr, MS_BIND | flags, nullptr);
            } else {
                const char* fstype = (pm.fs_type == "auto") ? "ext4" : pm.fs_type.c_str();
                mount(dev_path.c_str(), target.c_str(), fstype, flags | MS_NOATIME, nullptr);
            }
        }
    }
    return true;
}

bool ContainerEngine::start_container(const std::string& container_id) {
    auto it = configs_.find(container_id);
    if (it == configs_.end()) return false;
    const auto& cfg = it->second;

    std::string run_dir = "/run/fox_containers/" + container_id;
    mkdir(run_dir.c_str(), 0755);

    std::string upper_dir, work_dir, merged_dir;
    if (cfg.persistence_mode == StoragePersistenceMode::EPHEMERAL_RAM) {
        setup_ram_tmpfs(cfg, run_dir);
        upper_dir = run_dir + "/upper";
        work_dir = run_dir + "/work";
        merged_dir = run_dir + "/merged";
    } else {
        std::string data_dir = cfg.persistent_data_path.empty() ?
            ("/data/fox_containers/" + container_id) : cfg.persistent_data_path;
        mkdir(data_dir.c_str(), 0755);
        mkdir((data_dir + "/upper").c_str(), 0755);
        mkdir((data_dir + "/work").c_str(), 0755);
        mkdir((run_dir + "/merged").c_str(), 0755);
        upper_dir = data_dir + "/upper";
        work_dir = data_dir + "/work";
        merged_dir = run_dir + "/merged";
    }

    setup_overlayfs(cfg, merged_dir, upper_dir, work_dir);
    perform_partition_mounts(cfg, merged_dir);

    // Fork and isolate container inside namespaces
    pid_t pid = fork();
    if (pid == 0) {
        // Unshare mount, PID, UTS, IPC namespaces
        unshare(CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWIPC);

        // Mount /proc and /sys inside container rootfs
        mkdir((merged_dir + "/proc").c_str(), 0755);
        mount("proc", (merged_dir + "/proc").c_str(), "proc", 0, nullptr);

        mkdir((merged_dir + "/sys").c_str(), 0755);
        mount("sysfs", (merged_dir + "/sys").c_str(), "sysfs", 0, nullptr);

        mkdir((merged_dir + "/dev").c_str(), 0755);
        mount("/dev", (merged_dir + "/dev").c_str(), nullptr, MS_BIND, nullptr);

        // Set Hostname
        sethostname(cfg.hostname.c_str(), cfg.hostname.length());

        // Chroot and launch init command
        chroot(merged_dir.c_str());
        chdir("/");

        std::vector<char*> argv;
        for (const auto& arg : cfg.init_command) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(1);
    }

    ContainerStatus st;
    st.container_id = container_id;
    st.running = true;
    st.pid = pid;
    st.state = "RUNNING";
    statuses_[container_id] = st;
    return true;
}

bool ContainerEngine::stop_container(const std::string& container_id) {
    auto it = statuses_.find(container_id);
    if (it == statuses_.end() || !it->second.running) return false;

    if (it->second.pid > 0) {
        kill(it->second.pid, SIGTERM);
        usleep(200000);
        kill(it->second.pid, SIGKILL);
        waitpid(it->second.pid, nullptr, WNOHANG);
    }

    std::string run_dir = "/run/fox_containers/" + container_id;
    std::string merged_dir = run_dir + "/merged";
    umount2((merged_dir + "/proc").c_str(), MNT_DETACH);
    umount2((merged_dir + "/sys").c_str(), MNT_DETACH);
    umount2((merged_dir + "/dev").c_str(), MNT_DETACH);
    umount2(merged_dir.c_str(), MNT_DETACH);
    umount2(run_dir.c_str(), MNT_DETACH);

    it->second.running = false;
    it->second.pid = -1;
    it->second.state = "STOPPED";
    return true;
}

bool ContainerEngine::destroy_container(const std::string& container_id) {
    stop_container(container_id);
    configs_.erase(container_id);
    statuses_.erase(container_id);
    return true;
}

ContainerStatus ContainerEngine::get_container_status(const std::string& container_id) {
    auto it = statuses_.find(container_id);
    if (it != statuses_.end()) return it->second;
    ContainerStatus st;
    st.container_id = container_id;
    st.state = "NOT_FOUND";
    return st;
}

std::vector<ContainerStatus> ContainerEngine::list_containers() {
    std::vector<ContainerStatus> list;
    for (const auto& pair : statuses_) {
        list.push_back(pair.second);
    }
    return list;
}

std::string ContainerEngine::export_config_json(const ContainerConfig& cfg) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"container_id\": \"" << cfg.container_id << "\",\n"
        << "  \"display_name\": \"" << cfg.display_name << "\",\n"
        << "  \"distro\": \"" << cfg.distro << "\",\n"
        << "  \"base_image_path\": \"" << cfg.base_image_path << "\",\n"
        << "  \"persistence_mode\": \"" << (cfg.persistence_mode == StoragePersistenceMode::EPHEMERAL_RAM ? "EPHEMERAL_RAM" : "PERSISTENT_DATA") << "\",\n"
        << "  \"ram_upperdir_limit_mb\": " << cfg.ram_upperdir_limit_mb << ",\n"
        << "  \"hostname\": \"" << cfg.hostname << "\"\n"
        << "}\n";
    return oss.str();
}

ContainerConfig ContainerEngine::import_config_json(const std::string& json_str) {
    ContainerConfig cfg;
    // Basic parser for essential fields
    auto find_str = [&](const std::string& key) -> std::string {
        auto p = json_str.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        auto c = json_str.find(':', p);
        if (c == std::string::npos) return "";
        auto s1 = json_str.find('"', c);
        if (s1 == std::string::npos) return "";
        auto s2 = json_str.find('"', s1 + 1);
        if (s2 == std::string::npos) return "";
        return json_str.substr(s1 + 1, s2 - s1 - 1);
    };

    cfg.container_id = find_str("container_id");
    cfg.display_name = find_str("display_name");
    cfg.distro = find_str("distro");
    cfg.base_image_path = find_str("base_image_path");
    cfg.hostname = find_str("hostname");
    if (cfg.hostname.empty()) cfg.hostname = "m20lte-linux";
    return cfg;
}

} // namespace foxd
