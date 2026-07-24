// SPDX-License-Identifier: MIT
// edge-healthd: ethtool generic-netlink link-settings query
//
// Queries interface link speed/duplex via the "ethtool" generic-netlink family
// (ETHTOOL_MSG_LINKMODES_GET). This is the authoritative source the kernel also
// exposes via /sys/class/net/*/speed — used here over netlink for the richer,
// race-free reply.

#pragma once

#include "types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct mnl_socket; // libmnl, opaque here — the header stays include-clean

namespace edge {

struct EthtoolLink {
    std::optional<uint32_t> speed_mbps; // nullopt when unknown / link down
    std::optional<Duplex>   duplex;     // nullopt when unknown
};

// Holds a persistent NETLINK_GENERIC socket and the resolved "ethtool" family
// id so a long-running daemon amortises both across collection cycles (issue
// #59): the family id is a static ~1 KB definition that previously cost a
// round-trip every cycle, and the socket/bind/close churn is eliminated. The
// socket and family are opened/resolved lazily on first use and reused. A
// socket-level error tears the socket down so the next query transparently
// reopens it — a transient failure never becomes permanent degradation.
class EthtoolQuerier {
public:
    EthtoolQuerier() = default;
    ~EthtoolQuerier();
    EthtoolQuerier(const EthtoolQuerier&) = delete;
    EthtoolQuerier& operator=(const EthtoolQuerier&) = delete;

    // Query link settings for each interface in `ifnames`, issuing one
    // LINKMODES_GET per interface on the persistent socket. Returns a map keyed
    // by ifname; interfaces whose query fails (or that report SPEED_UNKNOWN /
    // DUPLEX_UNKNOWN) are simply absent or carry nullopt fields. Never throws;
    // on any setup failure returns empty (and retries setup on the next call).
    [[nodiscard]] std::unordered_map<std::string, EthtoolLink>
    query(const std::vector<std::string>& ifnames);

private:
    // Lazily open+bind the socket and resolve the family id; true when both are
    // ready. Keeps a bound socket across a failed family resolve so only the
    // resolve is retried next cycle.
    bool ensure_ready();
    // Close the socket and clear cached state, forcing a fresh open next time.
    void reset();

    struct mnl_socket* sock_ = nullptr;
    unsigned int       portid_ = 0;
    uint16_t           family_ = 0;
};

// One-shot convenience wrapper: constructs a throwaway EthtoolQuerier for a
// single batch. Prefer a long-lived EthtoolQuerier in daemon code so the socket
// and family id are reused. Never throws; on any setup failure returns empty.
[[nodiscard]] std::unordered_map<std::string, EthtoolLink>
ethtool_query_links(const std::vector<std::string>& ifnames);

} // namespace edge
