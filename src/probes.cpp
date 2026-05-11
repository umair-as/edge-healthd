// SPDX-License-Identifier: MIT
// edge-healthd: Probes (non-D-Bus) implementation

#include "probes.hpp"
#include "atomic_file.hpp"
#include "config.hpp"
#include "journal.hpp"
#include "log.hpp"
#include "netlink_monitor.hpp"

#include <nlohmann/json.hpp>
#include <unordered_map>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <dirent.h>
#include <linux/magic.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef EDGE_HAS_SYSTEMD
#include <systemd/sd-id128.h>
#include <systemd/sd-journal.h>
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
    // Prefer hardware serial from device-tree (stable across re-images)
    {
        std::ifstream file("/proc/device-tree/serial-number");
        if (file) {
            std::string id;
            std::getline(file, id);
            // device-tree strings are null-terminated; strip null and whitespace
            id.erase(std::remove_if(id.begin(), id.end(),
                         [](unsigned char c){ return c == '\0' || std::isspace(c); }),
                     id.end());
            if (!id.empty()) {
                return id;
            }
        }
    }

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
    if (ec) {
        log::probe_error("boot",
            "save_boot_state: create_directories(" + state_dir_.string() +
            ") failed: " + ec.message());
        return;
    }

    const auto path = state_dir_ / "boot_state.json";
    const nlohmann::json json = {
        {"last_boot_id", state.last_boot_id},
        {"consecutive_failures", state.consecutive_failures},
        {"last_boot_ok", state.last_boot_ok}
    };

    auto result = atomic_write_file(path, json.dump(2));
    if (!result) {
        // Surface the failure — the consecutive-failure ratchet depends on
        // this state surviving a power cut. A silent drop here masks a
        // boot loop that was about to trip boot_fail_crit.
        log::probe_error("boot",
            "save_boot_state: " + result.error().what());
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
    uint64_t mem_free_kb = 0;
    uint64_t buffers_kb = 0;
    uint64_t cached_kb = 0;
    uint64_t sreclaimable_kb = 0;
    uint64_t shmem_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;
    bool has_mem_total = false;
    bool has_mem_available = false;
    bool has_mem_free = false;
    bool has_buffers = false;
    bool has_cached = false;
    bool has_sreclaimable = false;
    bool has_shmem = false;
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
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value = 0;
        if (!(iss >> key >> value)) {
            continue;
        }

        if (key == "MemTotal:") {
            stats.mem_total_kb = value;
            stats.has_mem_total = true;
        } else if (key == "MemAvailable:") {
            stats.mem_available_kb = value;
            stats.has_mem_available = true;
        } else if (key == "MemFree:") {
            stats.mem_free_kb = value;
            stats.has_mem_free = true;
        } else if (key == "Buffers:") {
            stats.buffers_kb = value;
            stats.has_buffers = true;
        } else if (key == "Cached:") {
            stats.cached_kb = value;
            stats.has_cached = true;
        } else if (key == "SReclaimable:") {
            stats.sreclaimable_kb = value;
            stats.has_sreclaimable = true;
        } else if (key == "Shmem:") {
            stats.shmem_kb = value;
            stats.has_shmem = true;
        } else if (key == "SwapTotal:") {
            stats.swap_total_kb = value;
            stats.has_swap_total = true;
        } else if (key == "SwapFree:") {
            stats.swap_free_kb = value;
            stats.has_swap_free = true;
        }
    }

    if (!stats.has_mem_available && stats.has_mem_free &&
        stats.has_buffers && stats.has_cached &&
        stats.has_sreclaimable && stats.has_shmem) {
        // Fallback for kernels without MemAvailable.
        auto available =
            stats.mem_free_kb + stats.buffers_kb + stats.cached_kb +
            stats.sreclaimable_kb;
        if (available > stats.shmem_kb) {
            available -= stats.shmem_kb;
        } else {
            available = 0;
        }
        stats.mem_available_kb = available;
        stats.has_mem_available = true;
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

std::chrono::system_clock::time_point file_time_to_system_clock(
    std::filesystem::file_time_type tp) {
    using namespace std::chrono;
    const auto adjusted = tp - std::filesystem::file_time_type::clock::now()
        + system_clock::now();
    return time_point_cast<system_clock::duration>(adjusted);
}

std::string to_hex_u64(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << value;
    return oss.str();
}

std::string compute_fingerprint(const std::vector<CrashArtifact>& artifacts) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    uint64_t hash = kFnvOffset;

    for (const auto& artifact : artifacts) {
        std::ostringstream line;
        line << artifact.name << ":" << artifact.size_bytes << ":";
        if (artifact.mtime) {
            line << std::chrono::duration_cast<std::chrono::seconds>(
                artifact.mtime->time_since_epoch()).count();
        }
        const auto str = line.str();
        for (const unsigned char ch : str) {
            hash ^= static_cast<uint64_t>(ch);
            hash *= kFnvPrime;
        }
    }

    return to_hex_u64(hash);
}

// Convert IF_OPER_* code to schema string (matches IFLA_OPERSTATE values)
std::string_view operstate_to_string(uint8_t state) {
    switch (state) {
        case 0: return "unknown";       // IF_OPER_UNKNOWN
        case 1: return "notpresent";    // IF_OPER_NOTPRESENT
        case 2: return "down";          // IF_OPER_DOWN
        case 3: return "lowerlayerdown";// IF_OPER_LOWERLAYERDOWN
        case 4: return "testing";       // IF_OPER_TESTING
        case 5: return "dormant";       // IF_OPER_DORMANT
        case 6: return "up";            // IF_OPER_UP
        default: return "unknown";
    }
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


} // namespace

ResourcesProbe::ResourcesProbe(const Config& config,
                                     NetlinkMonitor& nl_monitor,
                                       std::span<const std::string> monitored_mounts,
                                       std::span<const std::string> monitored_interfaces)
    : config_(config)
    , nl_monitor_(nl_monitor) 
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
    MemoryUsage mem;
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;

    if (meminfo) {
        total_kb = meminfo->mem_total_kb;
        available_kb = meminfo->mem_available_kb;
        swap_total_kb = meminfo->swap_total_kb;
        swap_free_kb = meminfo->swap_free_kb;
    } else {
        // ProcSubset=pid may hide /proc/meminfo; use sysinfo(2) fallback.
        struct sysinfo si {};
        if (sysinfo(&si) != 0) {
            return std::unexpected(make_error(
                "resources",
                "Failed to read memory via /proc/meminfo and sysinfo()",
                err != 0 ? err : errno));
        }

        const uint64_t unit = si.mem_unit == 0 ? 1 : static_cast<uint64_t>(si.mem_unit);
        total_kb = (static_cast<uint64_t>(si.totalram) * unit) / 1024;
        const auto free_plus_buffer = static_cast<uint64_t>(si.freeram) + static_cast<uint64_t>(si.bufferram);
        available_kb = (free_plus_buffer * unit) / 1024;
        swap_total_kb = (static_cast<uint64_t>(si.totalswap) * unit) / 1024;
        swap_free_kb = (static_cast<uint64_t>(si.freeswap) * unit) / 1024;
    }

    mem.mem_total_mb = total_kb / 1024;
    if (total_kb >= available_kb) {
        mem.mem_used_mb = (total_kb - available_kb) / 1024;
    }

    if (swap_total_kb >= swap_free_kb) {
        mem.swap_used_mb = (swap_total_kb - swap_free_kb) / 1024;
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

    // 1. Access the live cache from the persistent monitor
    // No more socket opening/closing or kernel dumps here!
    auto nl_stats = nl_monitor_.get_all_stats();
    
    // 2. Build a map for quick lookup by interface name
    std::unordered_map<std::string, const NetlinkInterfaceStats*> nl_map;
    for (const auto& stats : nl_stats) {
        nl_map[stats.name] = &stats;
    }

    // When no interfaces are configured, report all non-loopback interfaces
    // present in the Netlink cache so the daemon works out of the box.
    std::vector<std::string> iface_list;
    if (monitored_interfaces_.empty()) {
        for (const auto& stats : nl_stats) {
            if (stats.name != "lo") {
                iface_list.push_back(stats.name);
            }
        }
    } else {
        iface_list = monitored_interfaces_;
    }

    for (const auto& ifname : iface_list) {
        NetworkInterface iface;
        iface.ifname = ifname;
        iface.link = LinkState::Unknown;

        if (!is_safe_ifname(ifname)) {
            interfaces.push_back(std::move(iface));
            continue;
        }

        // 3. Populate data from the Netlink Monitor cache
        auto it = nl_map.find(ifname);
        if (it != nl_map.end()) {
            const auto* nl = it->second;
            
            // Map state
            iface.link = nl->running ? LinkState::Up : LinkState::Down;
            iface.carrier = nl->carrier_up;
            
            // Map 64-bit stats
            iface.rx_bytes = nl->rx_bytes;
            iface.tx_bytes = nl->tx_bytes;
            iface.rx_packets = nl->rx_packets;
            iface.tx_packets = nl->tx_packets;
            iface.rx_dropped = nl->rx_dropped;
            iface.tx_dropped = nl->tx_dropped;
            iface.rx_err = nl->rx_errors;
            iface.tx_err = nl->tx_errors;

            // Map link metadata (free from IFLA_* — already in RTM_GETLINK messages)
            iface.mtu = nl->mtu;
            iface.mac = nl->mac;
            iface.operstate = std::string(operstate_to_string(nl->operstate));
            iface.carrier_changes = nl->carrier_changes;
            iface.carrier_up_count = nl->carrier_up_count;
            iface.carrier_down_count = nl->carrier_down_count;

        }
        // Note: We removed the ioctl fallback because the persistent Netlink
        // monitor is more reliable and handles state changes via multicast.

        // 4. Get IPv4 address from netlink cache (zero syscalls)
        if (it != nl_map.end() && it->second->ipv4_addr) {
            iface.ip = *it->second->ipv4_addr;
        }

        interfaces.push_back(std::move(iface));
    }

    return interfaces;
}
// -----------------------------------------------------------------------------
// UpdateProbe
// -----------------------------------------------------------------------------

UpdateProbe::UpdateProbe(const Config& config,
                         sdbus::IConnection* dbus,
                         std::filesystem::path state_dir)
    : config_(config)
    , dbus_(dbus)
    , state_dir_(std::move(state_dir)) {}

ProbeResult<UpdateStatus> UpdateProbe::collect() const {
    UpdateStatus status;
    status.overall = Severity::Ok;

    if (!config_.enable_update_tracking) {
        return status;
    }

    // Try live RAUC D-Bus data first; fall back to static file if unavailable.
    if (!collect_rauc_update(status)) {
        status.active_slot = detect_active_slot();
        status.last_update = load_last_update();

        if (status.last_update && status.last_update->result == UpdateResult::Failed) {
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

// -----------------------------------------------------------------------------
// JournalProbe
// -----------------------------------------------------------------------------

JournalProbe::JournalProbe(const Config& config)
    : config_(config) {}

ProbeResult<JournalStatus> JournalProbe::collect() const {
#ifdef EDGE_HAS_SYSTEMD
    JournalStatus status;
    status.overall = Severity::Ok;

    const int effective_priority = config_.log_excerpt_min_priority.value_or(3);
    const auto deadline =
        std::chrono::steady_clock::now() + config_.journal_scan_timeout;

    sd_journal* journal = nullptr;
    int ret = sd_journal_open(&journal, SD_JOURNAL_SYSTEM | SD_JOURNAL_CURRENT_USER);
    if (ret < 0 || !journal) {
        ret = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
        if (ret < 0 || !journal) {
            return status;
        }
    }

    struct JournalGuard {
        sd_journal* j;
        ~JournalGuard() { if (j) sd_journal_close(j); }
    } guard{journal};

    // Check budget after open — sd_journal_open() itself can block on large journals.
    if (std::chrono::steady_clock::now() >= deadline) {
        log::warn("journal scan skipped: sd_journal_open exceeded timeout ("
                  + std::to_string(config_.journal_scan_timeout.count()) + " ms)");
        return status;
    }

    ret = sd_journal_seek_tail(journal);
    if (ret < 0) {
        return status;
    }

    ret = sd_journal_previous(journal);
    if (ret <= 0) {
        return status;
    }

    const size_t max_scan = std::max<size_t>(
        config_.log_excerpt_max_lines ? config_.log_excerpt_max_lines : 20, 200);

    std::vector<JournalEntry> raw;
    raw.reserve(64);

    for (size_t i = 0; i < max_scan; ++i) {
        // Enforce deadline on every iteration — sd_journal_previous() cost is
        // proportional to the number of rotated journal files on disk.
        if (std::chrono::steady_clock::now() >= deadline) {
            log::warn("journal scan timeout after " + std::to_string(i)
                      + " entries (budget: "
                      + std::to_string(config_.journal_scan_timeout.count()) + " ms)");
            break;
        }

        // Read priority
        int pri = 6;
        const void* pdata = nullptr;
        size_t plen = 0;
        if (sd_journal_get_data(journal, "PRIORITY", &pdata, &plen) >= 0 && pdata) {
            const char* pstr = static_cast<const char*>(pdata);
            if (plen > 9 && std::strncmp(pstr, "PRIORITY=", 9) == 0) pstr += 9;
            pri = std::atoi(pstr);
        }

        if (pri <= effective_priority) {
            uint64_t usec = 0;
            sd_journal_get_realtime_usec(journal, &usec);

            const void* data = nullptr;
            size_t length = 0;
            std::string msg;
            if (sd_journal_get_data(journal, "MESSAGE", &data, &length) >= 0 && data) {
                const char* ptr = static_cast<const char*>(data);
                if (length > 8 && std::strncmp(ptr, "MESSAGE=", 8) == 0) {
                    ptr += 8;
                    length -= 8;
                }
                msg.assign(ptr, ptr + std::min(length, static_cast<size_t>(1024)));
            }
            raw.push_back(JournalEntry{usec, pri, std::move(msg)});
        }

        ret = sd_journal_previous(journal);
        if (ret <= 0) break;
    }

    status.error_count = static_cast<uint32_t>(raw.size());

    // Determine overall severity from the collected entries
    for (const auto& e : raw) {
        if (e.priority <= 2) {
            status.overall = Severity::Crit;
            break;
        }
        if (e.priority <= 3) {
            status.overall = Severity::Warn;
        }
    }

    // Apply window/max_lines trimming to produce recent_errors
    status.recent_errors = filter_journal_entries(config_, raw);

    return status;
#else
    return JournalStatus{};
#endif
}

// -----------------------------------------------------------------------------
// CrashProbe
// -----------------------------------------------------------------------------

CrashProbe::CrashProbe(const Config& config,
                       std::filesystem::path state_dir,
                       std::filesystem::path pstore_dir)
    : config_(config)
    , state_dir_(std::move(state_dir))
    , pstore_dir_(std::move(pstore_dir)) {}

ProbeResult<CrashStatus> CrashProbe::collect() const {
    (void)config_;
    CrashStatus status;

    std::vector<CrashArtifact> artifacts;
    if (std::error_code ec; std::filesystem::exists(pstore_dir_, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(pstore_dir_, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file(ec) || ec) {
                continue;
            }

            CrashArtifact artifact;
            artifact.name = entry.path().filename().string();
            artifact.size_bytes = entry.file_size(ec);
            if (ec) {
                artifact.size_bytes = 0;
                ec.clear();
            }
            const auto mtime = entry.last_write_time(ec);
            if (!ec) {
                artifact.mtime = file_time_to_system_clock(mtime);
            } else {
                ec.clear();
            }
            artifacts.push_back(std::move(artifact));
        }
    }

    std::sort(artifacts.begin(), artifacts.end(), [](const CrashArtifact& a, const CrashArtifact& b) {
        return a.name < b.name;
    });

    status.artifact_count = static_cast<uint32_t>(artifacts.size());
    status.artifacts = artifacts;
    status.present = !artifacts.empty();

    if (!status.present) {
        return status;
    }

    status.source = "pstore";
    auto latest = std::max_element(artifacts.begin(), artifacts.end(), [](const CrashArtifact& a, const CrashArtifact& b) {
        return a.mtime.value_or(std::chrono::system_clock::time_point{}) <
               b.mtime.value_or(std::chrono::system_clock::time_point{});
    });
    if (latest != artifacts.end()) {
        status.last_panic_at = latest->mtime;
    }

    status.fingerprint = compute_fingerprint(artifacts);

    std::optional<std::string> acknowledged_fp;
    const auto state_path = state_dir_ / "crash_state.json";
    {
        std::ifstream in(state_path);
        if (in) {
            try {
                const auto json = nlohmann::json::parse(in);
                if (json.contains("acknowledged_fingerprint")) {
                    acknowledged_fp = json["acknowledged_fingerprint"].get<std::string>();
                }
            } catch (const std::exception&) {
            }
        }
    }

    status.acknowledged = acknowledged_fp && status.fingerprint &&
                          *acknowledged_fp == *status.fingerprint;

    return status;
}

} // namespace edge
