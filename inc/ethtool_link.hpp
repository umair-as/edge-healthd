// SPDX-License-Identifier: MIT
// edge-healthd: ethtool generic-netlink link-settings query
//
// Queries interface link speed/duplex via the "ethtool" generic-netlink family
// (ETHTOOL_MSG_LINKMODES_GET). This is the authoritative source the kernel also
// exposes via /sys/class/net/*/speed — used here over netlink for the richer,
// race-free reply. One socket per query batch; the family id is resolved once.

#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace edge {

struct EthtoolLink {
    std::optional<uint32_t> speed_mbps; // nullopt when unknown / link down
    std::optional<Duplex>   duplex;     // nullopt when unknown
};

// Query link settings for each interface in `ifnames`. Opens a single
// NETLINK_GENERIC socket, resolves the ethtool family once, and issues one
// LINKMODES_GET per interface. Returns a map keyed by ifname; interfaces whose
// query fails (or that report SPEED_UNKNOWN / DUPLEX_UNKNOWN) are simply absent
// or carry nullopt fields. Never throws; on any setup failure returns empty.
[[nodiscard]] std::unordered_map<std::string, EthtoolLink>
ethtool_query_links(const std::vector<std::string>& ifnames);

} // namespace edge
