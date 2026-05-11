// SPDX-License-Identifier: MIT
// edge-healthd: Snapshot state aggregator
// Combines individual probe results into unified SnapshotState

#pragma once

#include "config.hpp"
#include "types.hpp"

#include <optional>
#include <vector>

namespace edge {

// -----------------------------------------------------------------------------
// SnapshotAggregator
//
// Combines results from all probes into a unified SnapshotState.
// Evaluates overall severity based on configured thresholds.
// -----------------------------------------------------------------------------

class SnapshotAggregator {
public:
    explicit SnapshotAggregator(const Config& config);

    // -------------------------------------------------------------------------
    // Main aggregation interface
    // -------------------------------------------------------------------------

    /// Aggregate all collected data into a SnapshotState
    [[nodiscard]] SnapshotState aggregate(
        const DeviceInfo& device,
        const BootStatus& boot,
        const ServicesStatus& services,
        const ResourcesStatus& resources,
        const TimeSyncStatus& time_sync,
        const UpdateStatus& update,
        const JournalStatus& journal,
        const CrashStatus& crash
    ) const;

    /// Aggregate with optional values (for partial collection scenarios)
    [[nodiscard]] SnapshotState aggregate_partial(
        std::optional<DeviceInfo> device,
        std::optional<BootStatus> boot,
        std::optional<ServicesStatus> services,
        std::optional<ResourcesStatus> resources,
        std::optional<TimeSyncStatus> time_sync,
        std::optional<UpdateStatus> update,
        std::optional<JournalStatus> journal,
        std::optional<CrashStatus> crash
    ) const;

    // -------------------------------------------------------------------------
    // Severity evaluation (public for testing)
    // -------------------------------------------------------------------------

    /// Evaluate boot status severity
    [[nodiscard]] Severity evaluate_boot(const BootStatus& boot) const;

    /// Evaluate services severity
    [[nodiscard]] Severity evaluate_services(const ServicesStatus& services) const;

    /// Evaluate resource usage severity
    [[nodiscard]] Severity evaluate_resources(const ResourcesStatus& resources) const;

    /// Evaluate time synchronization severity
    [[nodiscard]] Severity evaluate_time_sync(const TimeSyncStatus& time_sync) const;

    /// Evaluate update status severity
    [[nodiscard]] Severity evaluate_update(const UpdateStatus& update) const;

    /// Evaluate journal status severity
    [[nodiscard]] Severity evaluate_journal(const JournalStatus& journal) const;

    /// Evaluate crash status severity
    [[nodiscard]] Severity evaluate_crash(const CrashStatus& crash) const;

    /// Compute overall severity from individual severities
    [[nodiscard]] Severity compute_overall(
        Severity boot,
        Severity services,
        Severity resources,
        Severity time_sync,
        Severity update,
        Severity journal,
        Severity crash
    ) const;

private:
    const Config& config_;

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Generate summary reasons based on individual evaluations
    [[nodiscard]] std::vector<std::string> generate_reasons(
        const BootStatus& boot,
        const ServicesStatus& services,
        const ResourcesStatus& resources,
        const TimeSyncStatus& time_sync,
        const UpdateStatus& update,
        const JournalStatus& journal,
        const CrashStatus& crash
    ) const;

    /// Evaluate CPU load against thresholds
    [[nodiscard]] Severity evaluate_cpu(const CpuLoad& cpu) const;

    /// Evaluate memory usage against thresholds
    [[nodiscard]] Severity evaluate_memory(const MemoryUsage& memory) const;

    /// Evaluate disk usage against thresholds
    [[nodiscard]] Severity evaluate_disk(const std::vector<StorageMount>& storage) const;

    /// Evaluate thermal against thresholds
    [[nodiscard]] Severity evaluate_thermal(const std::vector<ThermalSensor>& thermal) const;

    /// Combine multiple severities (returns worst)
    [[nodiscard]] static Severity worst_of(std::initializer_list<Severity> severities);
};

} // namespace edge
