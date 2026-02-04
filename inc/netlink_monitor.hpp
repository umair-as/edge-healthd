// SPDX-License-Identifier: MIT
// edge-healthd: Netlink-based network monitoring (minimal, libmnl-based)
//
// This module uses libmnl to query network interface statistics via RTM_GETLINK.
// It extracts the following from IFLA_STATS64:
//   - rx/tx bytes, packets, errors, dropped (all required by schema)
//   - MTU, carrier state, link state (IFF_UP, IFF_RUNNING flags)

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map> 
#include <optional>

struct nlmsghdr;
struct mnl_socket;

namespace edge {

    struct NetlinkInterfaceStats {
    std::string name;
    int ifindex{-1}; // Needed to correlate Ethtool data
    
    // Stats from IFLA_STATS64
    uint64_t rx_bytes{0}, tx_bytes{0};
    uint64_t rx_packets{0}, tx_packets{0};
    uint64_t rx_errors{0}, tx_errors{0};
    uint64_t rx_dropped{0}, tx_dropped{0};
    
    // Link state
    bool link_up{false};    // IFF_UP
    bool running{false};    // IFF_RUNNING
    bool carrier_up{false}; // IFLA_CARRIER
    
    // Link metadata (from IFLA_* attributes in RTM_GETLINK)
    uint32_t mtu{0};
    std::string mac;            // "aa:bb:cc:dd:ee:ff"
    uint8_t operstate{0};       // IF_OPER_* (0=unknown, 6=up)
    uint32_t carrier_changes{0};
    uint32_t carrier_up_count{0};
    uint32_t carrier_down_count{0};

    std::optional<std::string> ipv4_addr;  // Cached from RTM_GETADDR
};

class NetlinkMonitor {
public:
    NetlinkMonitor();
    ~NetlinkMonitor();

    // Initializes the socket and subscribes to multicast groups
    bool init();

    // Returns the file descriptor for the event loop (sd-event)
    int get_fd() const { return fd_; }

    // Called when the event loop detects data on the FD
    void process_incoming();

    // Provides the latest cached data to the ResourcesProbe
    std::vector<NetlinkInterfaceStats> get_all_stats() const;

private:
    int fd_{-1};
    std::map<int, NetlinkInterfaceStats> cache_; // State indexed by ifindex

    // NETLINK_ROUTE helpers
    void request_dump();
    void request_addr_dump();
    void drain_response();

    ::mnl_socket* fd_ptr_{nullptr};     // NETLINK_ROUTE

};
} // namespace edge
