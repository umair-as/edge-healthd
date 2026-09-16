// SPDX-License-Identifier: MIT
// edge-healthd: JSON serialization declarations

#pragma once

#include "types.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string_view>

namespace edge {

// ADL-visible conversions for nlohmann::json.
void to_json(nlohmann::json& j, const OsInfo& os);
void to_json(nlohmann::json& j, const DeviceInfo& device);
void to_json(nlohmann::json& j, const BootStatus& boot);
void to_json(nlohmann::json& j, const ServiceUnit& unit);
void to_json(nlohmann::json& j, const ServicesStatus& services);
void to_json(nlohmann::json& j, const CpuLoad& cpu);
void to_json(nlohmann::json& j, const MemoryUsage& memory);
void to_json(nlohmann::json& j, const StorageMount& mount);
void to_json(nlohmann::json& j, const ThermalSensor& sensor);
void to_json(nlohmann::json& j, const NetworkInterface& iface);
void to_json(nlohmann::json& j, const ResourcesStatus& resources);
void to_json(nlohmann::json& j, const NtpStatus& ntp);
void to_json(nlohmann::json& j, const PtpStatus& ptp);
void to_json(nlohmann::json& j, const TimeSyncStatus& time_sync);
void to_json(nlohmann::json& j, const LastUpdate& update);
void to_json(nlohmann::json& j, const UpdateStatus& update);
void to_json(nlohmann::json& j, const JournalStatus& journal);
void to_json(nlohmann::json& j, const CrashArtifact& artifact);
void to_json(nlohmann::json& j, const CrashStatus& crash);
void to_json(nlohmann::json& j, const SnapshotSummary& summary);
void to_json(nlohmann::json& j, const SnapshotState& state);

namespace json {

[[nodiscard]] std::string serialize(const SnapshotState& state);
[[nodiscard]] std::string serialize_pretty(const SnapshotState& state, int indent = 2);

// Parse a UTC timestamp in the snapshot's own format ("2026-03-03T09:19:39Z").
// Returns nullopt for anything else, including trailing characters.
[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_time(std::string_view text);

} // namespace json

} // namespace edge
