// SPDX-License-Identifier: MIT
// edge-healthd: Aggregator implementation

#include "aggregator.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace edge {

SnapshotAggregator::SnapshotAggregator(const Config& config)
    : config_(config) {}

SnapshotState SnapshotAggregator::aggregate(
    const DeviceInfo& device,
    const BootStatus& boot,
    const ServicesStatus& services,
    const ResourcesStatus& resources,
    const TimeSyncStatus& time_sync,
    const UpdateStatus& update
) const {
    SnapshotState state;
    state.generated_at = std::chrono::system_clock::now();
    state.device = device;
    state.boot = boot;
    state.services = services;
    state.resources = resources;
    state.time_sync = time_sync;
    state.update = update;

    // Evaluate severities
    auto boot_sev = evaluate_boot(boot);
    auto services_sev = evaluate_services(services);
    auto resources_sev = evaluate_resources(resources);
    auto time_sync_sev = evaluate_time_sync(time_sync);
    auto update_sev = evaluate_update(update);

    // Compute overall
    state.summary.severity = compute_overall(
        boot_sev, services_sev, resources_sev, time_sync_sev, update_sev);
    state.summary.reasons = generate_reasons(
        boot, services, resources, time_sync, update);

    return state;
}

SnapshotState SnapshotAggregator::aggregate_partial(
    std::optional<DeviceInfo> device,
    std::optional<BootStatus> boot,
    std::optional<ServicesStatus> services,
    std::optional<ResourcesStatus> resources,
    std::optional<TimeSyncStatus> time_sync,
    std::optional<UpdateStatus> update
) const {
    return aggregate(
        device.value_or(DeviceInfo{}),
        boot.value_or(BootStatus{}),
        services.value_or(ServicesStatus{}),
        resources.value_or(ResourcesStatus{}),
        time_sync.value_or(TimeSyncStatus{}),
        update.value_or(UpdateStatus{})
    );
}

Severity SnapshotAggregator::evaluate_boot(const BootStatus& boot) const {
    if (boot.boot_fail_count >= config_.thresholds.boot_fail_crit) {
        return Severity::Crit;
    }
    if (boot.boot_fail_count >= config_.thresholds.boot_fail_warn) {
        return Severity::Warn;
    }
    if (!boot.boot_ok) {
        return Severity::Warn;
    }
    return Severity::Ok;
}

Severity SnapshotAggregator::evaluate_services(const ServicesStatus& services) const {
    return services.overall;
}

Severity SnapshotAggregator::evaluate_resources(const ResourcesStatus& resources) const {
    auto cpu_sev = evaluate_cpu(resources.cpu);
    auto mem_sev = evaluate_memory(resources.memory);
    auto disk_sev = evaluate_disk(resources.storage);
    auto thermal_sev = evaluate_thermal(resources.thermal);

    return worst_of({cpu_sev, mem_sev, disk_sev, thermal_sev});
}

Severity SnapshotAggregator::evaluate_time_sync(const TimeSyncStatus& time_sync) const {
    return time_sync.overall;
}

Severity SnapshotAggregator::evaluate_update(const UpdateStatus& update) const {
    return update.overall;
}

Severity SnapshotAggregator::compute_overall(
    Severity boot,
    Severity services,
    Severity resources,
    Severity time_sync,
    Severity update
) const {
    return worst_of({boot, services, resources, time_sync, update});
}

std::vector<std::string> SnapshotAggregator::generate_reasons(
    const BootStatus& boot,
    const ServicesStatus& services,
    const ResourcesStatus& resources,
    const TimeSyncStatus& time_sync,
    const UpdateStatus& update
) const {
    std::vector<std::string> reasons;

    if (boot.boot_fail_count > 0) {
        reasons.push_back("boot_failures");
    }

    for (const auto& unit : services.units) {
        if (unit.state == ServiceState::Failed) {
            reasons.push_back("svc_failed:" + unit.name);
        }
    }

    // Check resource thresholds
    auto& t = config_.thresholds;

    // CPU check would need core count - simplified here
    if (resources.memory.mem_total_mb > 0) {
        auto mem_pct = (resources.memory.mem_used_mb * 100) / resources.memory.mem_total_mb;
        if (mem_pct >= t.mem_used_warn) {
            reasons.push_back("mem_used_high");
        }
    }

    for (const auto& mount : resources.storage) {
        if (mount.used_pct >= t.disk_used_warn) {
            reasons.push_back("disk_used_high:" + mount.mount);
        }
    }

    for (const auto& sensor : resources.thermal) {
        if (sensor.temp_c >= t.temp_warn_c) {
            if (!sensor.sensor.empty()) {
                reasons.push_back("temp_high:" + sensor.sensor);
            } else {
                reasons.push_back("temp_high");
            }
        }
    }

    if (time_sync.source == TimeSyncSource::None) {
        reasons.push_back("time_unsynced");
    }

    if (update.last_update && update.last_update->result == UpdateResult::Failed) {
        reasons.push_back("update_failed");
    }

    return reasons;
}

Severity SnapshotAggregator::evaluate_cpu(const CpuLoad& cpu) const {
    auto cores = std::max(1u, std::thread::hardware_concurrency());
    auto load_pct = (cpu.load1 / static_cast<double>(cores)) * 100.0;

    if (load_pct >= config_.thresholds.cpu_load_crit) {
        return Severity::Crit;
    }
    if (load_pct >= config_.thresholds.cpu_load_warn) {
        return Severity::Warn;
    }
    return Severity::Ok;
}

Severity SnapshotAggregator::evaluate_memory(const MemoryUsage& memory) const {
    if (memory.mem_total_mb == 0) {
        return Severity::Unknown;
    }

    auto pct = (memory.mem_used_mb * 100) / memory.mem_total_mb;

    if (pct >= config_.thresholds.mem_used_crit) {
        return Severity::Crit;
    }
    if (pct >= config_.thresholds.mem_used_warn) {
        return Severity::Warn;
    }
    return Severity::Ok;
}

Severity SnapshotAggregator::evaluate_disk(const std::vector<StorageMount>& storage) const {
    Severity worst = Severity::Ok;

    for (const auto& mount : storage) {
        if (mount.used_pct >= config_.thresholds.disk_used_crit) {
            worst = worst_of({worst, Severity::Crit});
        } else if (mount.used_pct >= config_.thresholds.disk_used_warn) {
            worst = worst_of({worst, Severity::Warn});
        }
    }

    return worst;
}

Severity SnapshotAggregator::evaluate_thermal(const std::vector<ThermalSensor>& thermal) const {
    Severity worst = Severity::Ok;

    for (const auto& sensor : thermal) {
        if (sensor.temp_c >= config_.thresholds.temp_crit_c) {
            worst = worst_of({worst, Severity::Crit});
        } else if (sensor.temp_c >= config_.thresholds.temp_warn_c) {
            worst = worst_of({worst, Severity::Warn});
        }
    }

    return worst;
}

Severity SnapshotAggregator::worst_of(std::initializer_list<Severity> severities) {
    Severity worst = Severity::Unknown;
    int worst_rank = -1;

    for (auto s : severities) {
        int rank = 0;
        switch (s) {
            case Severity::Unknown: rank = 0; break;
            case Severity::Ok: rank = 1; break;
            case Severity::Warn: rank = 2; break;
            case Severity::Crit: rank = 3; break;
        }
        if (rank > worst_rank) {
            worst_rank = rank;
            worst = s;
        }
    }

    return worst;
}



} // namespace edge
