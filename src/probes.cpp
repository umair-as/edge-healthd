// SPDX-License-Identifier: MIT
// edge-healthd: Probes (non-D-Bus) implementation

#include "probes.hpp"
#include "config.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <dirent.h>
#include <ifaddrs.h>
#include <linux/magic.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-id128.h>
#endif

namespace edge {

// -----------------------------------------------------------------------------
// DeviceProbe
// -----------------------------------------------------------------------------

DeviceProbe::DeviceProbe(const Config& config)
    : config_(config) {}

ProbeResult<DeviceInfo> DeviceProbe::collect() const {
    DeviceInfo info;

    // Use configured device_id and platform, or detect
    info.device_id = config_.device_id.empty() ? read_machine_id() : config_.device_id;
    info.platform = config_.platform;
    info.hostname = read_hostname();
    info.os = read_os_release();

    // Get architecture from uname
    struct utsname uts{};
    if (uname(&uts) == 0) {
        info.arch = uts.machine;
        if (info.os.kernel.empty()) {
            info.os.kernel = uts.release;
        }
    }

    return info;
}

OsInfo DeviceProbe::read_os_release() const {
    OsInfo os;

    std::ifstream file("/etc/os-release");
    if (!file) {
        return os;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Parse KEY=VALUE or KEY="VALUE"
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        auto key = line.substr(0, pos);
        auto value = line.substr(pos + 1);

        // Remove quotes
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "ID") {
            os.distro = value;
        } else if (key == "VERSION_ID") {
            os.version = value;
        } else if (key == "BUILD_ID") {
            os.build_id = value;
        }
    }

    // Get kernel version
    struct utsname uts{};
    if (uname(&uts) == 0) {
        os.kernel = uts.release;
    }

    return os;
}

std::string DeviceProbe::read_machine_id() const {
    std::ifstream file("/etc/machine-id");
    if (!file) {
        return "unknown";
    }

    std::string id;
    std::getline(file, id);

    // Trim whitespace
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) {
        id.pop_back();
    }

    return id.empty() ? "unknown" : id;
}

std::string DeviceProbe::read_hostname() const {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        return hostname;
    }

    // Fallback to /etc/hostname
    std::ifstream file("/etc/hostname");
    if (file) {
        std::string name;
        std::getline(file, name);
        return name;
    }

    return "unknown";
}

// -----------------------------------------------------------------------------
// BootProbe
// -----------------------------------------------------------------------------

BootProbe::BootProbe(const Config& config, std::filesystem::path state_dir)
    : config_(config)
    , state_dir_(std::move(state_dir)) {}

ProbeResult<BootStatus> BootProbe::collect() const {
    BootStatus status;

    status.boot_id = read_boot_id();
    status.uptime = read_uptime();
    status.last_boot_at = std::chrono::system_clock::now() - status.uptime;

    // Load persistent boot state
    auto state = load_boot_state();

    // Check if this is a new boot
    if (state.last_boot_id != status.boot_id) {
        if (!state.last_boot_id.empty() && !state.last_boot_ok) {
            state.consecutive_failures += 1;
        }
        state.last_boot_id = status.boot_id;
        state.last_boot_ok = false;
        save_boot_state(state);
    }

    status.boot_fail_count = state.consecutive_failures;
    status.boot_ok = (state.consecutive_failures == 0);

    return status;
}

void BootProbe::mark_boot_success() {
    auto state = load_boot_state();
    state.last_boot_id = read_boot_id();
    state.consecutive_failures = 0;
    state.last_boot_ok = true;
    save_boot_state(state);
}

BootProbe::BootState BootProbe::load_boot_state() const {
    BootState state;

    auto path = state_dir_ / "boot_state.json";
    std::ifstream file(path);

    if (!file) {
        return state;
    }

    try {
        auto json = nlohmann::json::parse(file);

        if (json.contains("last_boot_id")) {
            state.last_boot_id = json["last_boot_id"].get<std::string>();
        }
        if (json.contains("consecutive_failures")) {
            state.consecutive_failures = json["consecutive_failures"].get<uint32_t>();
        }
        if (json.contains("last_boot_ok")) {
            state.last_boot_ok = json["last_boot_ok"].get<bool>();
        } else {
            state.last_boot_ok = (state.consecutive_failures == 0);
        }
    } catch (const std::exception&) {
        // Corrupted state file, start fresh
    }

    return state;
}

void BootProbe::save_boot_state(const BootState& state) const {
    // Ensure directory exists
    std::error_code ec;
    std::filesystem::create_directories(state_dir_, ec);

    auto path = state_dir_ / "boot_state.json";
    auto tmp_path = path.string() + ".tmp";

    nlohmann::json json = {
        {"last_boot_id", state.last_boot_id},
        {"consecutive_failures", state.consecutive_failures},
        {"last_boot_ok", state.last_boot_ok}
    };

    std::ofstream file(tmp_path);
    if (file) {
        file << json.dump(2);
        file.close();
        std::filesystem::rename(tmp_path, path, ec);
    }
}

std::string BootProbe::read_boot_id() const {
#ifdef EDGE_HAS_SYSTEMD
    sd_id128_t boot_id;
    if (sd_id128_get_boot(&boot_id) >= 0) {
        char str[SD_ID128_STRING_MAX];
        sd_id128_to_string(boot_id, str);
        return str;
    }
#endif
    return "unknown";
}

std::chrono::seconds BootProbe::read_uptime() const {
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        return std::chrono::seconds(0);
    }
    return std::chrono::seconds(static_cast<int64_t>(ts.tv_sec));
}

// -----------------------------------------------------------------------------
// ResourcesProbe
// -----------------------------------------------------------------------------

namespace {
struct MemInfoStats {
    uint64_t mem_total_kb = 0;
    uint64_t mem_available_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;
    bool has_mem_total = false;
    bool has_mem_available = false;
    bool has_swap_total = false;
    bool has_swap_free = false;
};

std::optional<MemInfoStats> read_meminfo(int* err_out) {
    std::ifstream file("/proc/meminfo");
    if (!file) {
        if (err_out) {
            *err_out = errno;
        }
        return std::nullopt;
    }

    MemInfoStats stats;
    std::string key;
    uint64_t value = 0;
    std::string unit;

    while (file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            stats.mem_total_kb = value;
            stats.has_mem_total = true;
        } else if (key == "MemAvailable:") {
            stats.mem_available_kb = value;
            stats.has_mem_available = true;
        } else if (key == "SwapTotal:") {
            stats.swap_total_kb = value;
            stats.has_swap_total = true;
        } else if (key == "SwapFree:") {
            stats.swap_free_kb = value;
            stats.has_swap_free = true;
        }

        if (stats.has_mem_total && stats.has_mem_available &&
            stats.has_swap_total && stats.has_swap_free) {
            break;
        }
    }

    if (!stats.has_mem_total || !stats.has_mem_available) {
        if (err_out) {
            *err_out = 0;
        }
        return std::nullopt;
    }

    if (err_out) {
        *err_out = 0;
    }
    return stats;
}

bool is_safe_ifname(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (char ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) &&
            ch != '.' && ch != '-' && ch != '_' && ch != '@' && ch != ':') {
            return false;
        }
    }
    return true;
}

std::optional<std::string> get_ipv4_for_interface(const std::string& ifname) {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        return std::nullopt;
    }

    std::optional<std::string> result;
    for (auto* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifname != ifa->ifa_name) {
            continue;
        }
        char host[NI_MAXHOST];
        if (getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in),
                        host, sizeof(host),
                        nullptr, 0, NI_NUMERICHOST) == 0) {
            result = host;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return result;
}

std::string fs_type_to_string(long type) {
    switch (type) {
        case EXT4_SUPER_MAGIC: return "ext4";
        case TMPFS_MAGIC: return "tmpfs";
        case NFS_SUPER_MAGIC: return "nfs";
        case BTRFS_SUPER_MAGIC: return "btrfs";
        case XFS_SUPER_MAGIC: return "xfs";
        case MSDOS_SUPER_MAGIC: return "vfat";
        case OVERLAYFS_SUPER_MAGIC: return "overlay";
        default: return {};
    }
}

LinkState link_state_from_flags(const std::string& ifname) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return LinkState::Unknown;
    }

    struct ifreq ifr{};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname.c_str());
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
        close(fd);
        return LinkState::Unknown;
    }
    close(fd);

    if (ifr.ifr_flags & IFF_UP) {
        return LinkState::Up;
    }
    return LinkState::Down;
}
} // namespace

ResourcesProbe::ResourcesProbe(const Config& config,
                                       std::span<const std::string> monitored_mounts,
                                       std::span<const std::string> monitored_interfaces)
    : config_(config)
    , monitored_mounts_(monitored_mounts.begin(), monitored_mounts.end())
    , monitored_interfaces_(monitored_interfaces.begin(), monitored_interfaces.end()) {

    // Use config defaults if not specified
    if (monitored_mounts_.empty()) {
        monitored_mounts_ = config_.monitored_mounts;
    }
    if (monitored_interfaces_.empty()) {
        monitored_interfaces_ = config_.monitored_interfaces;
    }
}

ProbeResult<ResourcesStatus> ResourcesProbe::collect() const {
    ResourcesStatus status;
    status.sample_window_sec = config_.sample_window_sec;

    status.cpu = collect_cpu_load();

    auto memory_result = collect_memory();
    if (!memory_result) {
        return std::unexpected(memory_result.error());
    }
    status.memory = *memory_result;

    status.storage = collect_storage();

    if (config_.enable_thermal) {
        status.thermal = collect_thermal();
    }

    status.network = collect_network();

    return status;
}

CpuLoad ResourcesProbe::collect_cpu_load() const {
    CpuLoad load;
    double values[3] = {};
    if (getloadavg(values, 3) == 3) {
        load.load1 = values[0];
        load.load5 = values[1];
        load.load15 = values[2];
    }

    return load;
}

ProbeResult<MemoryUsage> ResourcesProbe::collect_memory() const {
    int err = 0;
    auto meminfo = read_meminfo(&err);
    if (!meminfo) {
        return std::unexpected(make_error(
            "resources",
            "Failed to read MemTotal/MemAvailable from /proc/meminfo",
            err));
    }

    MemoryUsage mem;
    const uint64_t total_kb = meminfo->mem_total_kb;
    const uint64_t available_kb = meminfo->mem_available_kb;

    mem.mem_total_mb = total_kb / 1024;
    if (total_kb >= available_kb) {
        mem.mem_used_mb = (total_kb - available_kb) / 1024;
    }

    if (meminfo->swap_total_kb >= meminfo->swap_free_kb) {
        mem.swap_used_mb = (meminfo->swap_total_kb - meminfo->swap_free_kb) / 1024;
    }

    return mem;
}

std::vector<StorageMount> ResourcesProbe::collect_storage() const {
    std::vector<StorageMount> mounts;

    for (const auto& mount_path : monitored_mounts_) {
        StorageMount mount;
        mount.mount = mount_path;

        struct statvfs stat{};
        if (statvfs(mount_path.c_str(), &stat) == 0) {
            uint64_t total = stat.f_blocks * stat.f_frsize;
            uint64_t free = stat.f_bfree * stat.f_frsize;
            uint64_t avail = stat.f_bavail * stat.f_frsize;

            if (total > 0) {
                uint64_t used = total - free;
                mount.used_pct = static_cast<uint8_t>((used * 100) / total);
                mount.avail_mb = avail / (1024 * 1024);
            }
        }

        struct statfs fs_stat{};
        if (statfs(mount_path.c_str(), &fs_stat) == 0) {
            mount.fs = fs_type_to_string(fs_stat.f_type);
        }

        mounts.push_back(std::move(mount));
    }

    return mounts;
}

std::vector<ThermalSensor> ResourcesProbe::collect_thermal() const {
    std::vector<ThermalSensor> sensors;

    // Read from /sys/class/thermal/thermal_zone*/
    const char* thermal_path = "/sys/class/thermal";
    DIR* dir = opendir(thermal_path);
    if (!dir) {
        return sensors;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("thermal_zone") != 0) {
            continue;
        }

        ThermalSensor sensor;

        // Read type
        std::string type_path = std::string(thermal_path) + "/" + name + "/type";
        std::ifstream type_file(type_path);
        if (type_file) {
            std::getline(type_file, sensor.sensor);
        } else {
            sensor.sensor = name;
        }

        // Read temperature
        std::string temp_path = std::string(thermal_path) + "/" + name + "/temp";
        std::ifstream temp_file(temp_path);
        if (temp_file) {
            int millidegrees = 0;
            temp_file >> millidegrees;
            sensor.temp_c = millidegrees / 1000.0;
        }

        sensors.push_back(std::move(sensor));
    }

    closedir(dir);
    return sensors;
}

std::vector<NetworkInterface> ResourcesProbe::collect_network() const {
    std::vector<NetworkInterface> interfaces;

    for (const auto& ifname : monitored_interfaces_) {
        NetworkInterface iface;
        iface.ifname = ifname;
        iface.link = LinkState::Unknown;

        if (!is_safe_ifname(ifname)) {
            interfaces.push_back(std::move(iface));
            continue;
        }

        iface.link = link_state_from_flags(ifname);

        // Read error counters from /sys/class/net/<if>/statistics/
        std::string rx_err_path = "/sys/class/net/" + ifname + "/statistics/rx_errors";
        std::ifstream rx_err_file(rx_err_path);
        if (rx_err_file) {
            rx_err_file >> iface.rx_err;
        }

        std::string tx_err_path = "/sys/class/net/" + ifname + "/statistics/tx_errors";
        std::ifstream tx_err_file(tx_err_path);
        if (tx_err_file) {
            tx_err_file >> iface.tx_err;
        }

        if (auto ip = get_ipv4_for_interface(ifname)) {
            iface.ip = *ip;
        }

        interfaces.push_back(std::move(iface));
    }

    return interfaces;
}

// -----------------------------------------------------------------------------
// UpdateProbe
// -----------------------------------------------------------------------------

UpdateProbe::UpdateProbe(const Config& config, std::filesystem::path state_dir)
    : config_(config)
    , state_dir_(std::move(state_dir)) {}

ProbeResult<UpdateStatus> UpdateProbe::collect() const {
    UpdateStatus status;
    status.overall = Severity::Ok;

    if (!config_.enable_update_tracking) {
        return status;
    }

    status.active_slot = detect_active_slot();
    status.last_update = load_last_update();

    // Evaluate severity based on last update
    if (status.last_update) {
        if (status.last_update->result == UpdateResult::Failed) {
            status.overall = Severity::Warn;
        }
    }

    return status;
}

std::optional<std::string> UpdateProbe::detect_active_slot() const {
    // Placeholder until a D-Bus integration (e.g., RAUC) is added.
    return std::nullopt;
}

std::optional<LastUpdate> UpdateProbe::load_last_update() const {
    auto path = state_dir_ / "last_update.json";
    std::ifstream file(path);

    if (!file) {
        return std::nullopt;
    }

    try {
        auto json = nlohmann::json::parse(file);
        LastUpdate update;

        if (json.contains("id")) {
            update.id = json["id"].get<std::string>();
        }

        if (json.contains("installed_at")) {
            // Parse ISO 8601 timestamp
            // Simplified - production would use proper parsing
            update.installed_at = std::chrono::system_clock::now();
        }

        if (json.contains("result")) {
            std::string result = json["result"].get<std::string>();
            if (result == "success") {
                update.result = UpdateResult::Success;
            } else if (result == "failed") {
                update.result = UpdateResult::Failed;
            }
        }

        if (json.contains("detail")) {
            update.detail = json["detail"].get<std::string>();
        }

        return update;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace edge
