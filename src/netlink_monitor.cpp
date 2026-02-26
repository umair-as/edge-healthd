// SPDX-License-Identifier: MIT
// edge-healthd: Netlink-based network monitoring (minimal, libmnl-based)

#include "netlink_monitor.hpp"

#ifdef EDGE_HAS_LIBMNL

#include <libmnl/libmnl.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/if.h>
#include <linux/if_addr.h>
#include <arpa/inet.h>
#include <ctime>

#include <cerrno>
#include <cstring>
#include <memory>
#include <vector>

#include <fcntl.h>
#include <poll.h>

namespace edge {

namespace {


    // Format a 6-byte MAC address as "aa:bb:cc:dd:ee:ff"
    std::string format_mac(const void* addr, uint16_t len) {
        if (len != 6) return {};
        const auto* bytes = static_cast<const uint8_t*>(addr);
        char buf[18]; // "xx:xx:xx:xx:xx:xx\0"
        std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                       bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        return buf;
    }

    // Helper to extract stats and metadata from the RTNL message
    void parse_rtnl_stats(struct nlattr* attr, NetlinkInterfaceStats& stats) {
        switch (mnl_attr_get_type(attr)) {
        case IFLA_STATS64:
            if (mnl_attr_validate(attr, MNL_TYPE_UNSPEC) >= 0) {
                const auto* s = static_cast<const struct rtnl_link_stats64*>(mnl_attr_get_payload(attr));
                stats.rx_bytes = s->rx_bytes;
                stats.tx_bytes = s->tx_bytes;
                stats.rx_packets = s->rx_packets;
                stats.tx_packets = s->tx_packets;
                stats.rx_errors = s->rx_errors;
                stats.tx_errors = s->tx_errors;
                stats.rx_dropped = s->rx_dropped;
                stats.tx_dropped = s->tx_dropped;
            }
            break;
        case IFLA_IFNAME:
            stats.name = mnl_attr_get_str(attr);
            break;
        case IFLA_CARRIER:
            stats.carrier_up = (mnl_attr_get_u8(attr) != 0);
            break;
        case IFLA_MTU:
            stats.mtu = mnl_attr_get_u32(attr);
            break;
        case IFLA_ADDRESS: {
            auto mac = format_mac(mnl_attr_get_payload(attr), mnl_attr_get_payload_len(attr));
            if (!mac.empty()) stats.mac = std::move(mac);
            break;
        }
        case IFLA_OPERSTATE:
            stats.operstate = mnl_attr_get_u8(attr);
            break;
        case IFLA_CARRIER_CHANGES:
            stats.carrier_changes = mnl_attr_get_u32(attr);
            break;
        case IFLA_CARRIER_UP_COUNT:
            stats.carrier_up_count = mnl_attr_get_u32(attr);
            break;
        case IFLA_CARRIER_DOWN_COUNT:
            stats.carrier_down_count = mnl_attr_get_u32(attr);
            break;
        default:
            break;
        }
    }

    // Callback used for both initial dump and runtime updates
    int data_cb(const struct nlmsghdr* nlh, void* data) {
        auto* cache = static_cast<std::map<int, NetlinkInterfaceStats>*>(data);
        struct ifinfomsg* ifm = static_cast<struct ifinfomsg*>(mnl_nlmsg_get_payload(nlh));
        
        // Find or create entry in our persistent cache
        NetlinkInterfaceStats& stats = (*cache)[ifm->ifi_index];
        stats.ifindex = ifm->ifi_index;
        stats.link_up = (ifm->ifi_flags & IFF_UP) != 0;
        stats.running = (ifm->ifi_flags & IFF_RUNNING) != 0;

        auto* payload = static_cast<struct nlattr*>(
            mnl_nlmsg_get_payload_offset(nlh, sizeof(*ifm)));
        auto* payload_tail = static_cast<char*>(mnl_nlmsg_get_payload_tail(nlh));
        size_t payload_size = static_cast<size_t>(
            payload_tail - reinterpret_cast<char*>(payload));
        for (struct nlattr* attr = payload;
             mnl_attr_ok(attr, payload_size);
             attr = mnl_attr_next(attr)) {
            parse_rtnl_stats(attr, stats);
        }
        return MNL_CB_OK;
    }

    // inet_ntop wrapper — adapted from NetlinkSockets util.hpp
    std::string format_addr(const void* addr, uint8_t family, uint16_t len) {
        if (family == AF_INET && len != 4) return {};
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(family, addr, buf, sizeof(buf))) {
            return buf;
        }
        return {};
    }

    // Callback for RTM_NEWADDR / RTM_DELADDR messages
    // Adapted from NetlinkSockets netlink_parse.cpp addr_attr_cb
    int addr_data_cb(const struct nlmsghdr* nlh, void* data) {
        auto* cache = static_cast<std::map<int, NetlinkInterfaceStats>*>(data);
        auto* ifa = static_cast<struct ifaddrmsg*>(mnl_nlmsg_get_payload(nlh));

        // Only handle IPv4 (matches previous getifaddrs behaviour)
        if (ifa->ifa_family != AF_INET) return MNL_CB_OK;

        int ifindex = static_cast<int>(ifa->ifa_index);

        // Ignore addresses for interfaces we haven't seen via RTM_GETLINK
        auto it = cache->find(ifindex);
        if (it == cache->end()) return MNL_CB_OK;

        if (nlh->nlmsg_type == RTM_DELADDR) {
            it->second.ipv4_addr.reset();
            return MNL_CB_OK;
        }

        // RTM_NEWADDR: parse IFA_ADDRESS and IFA_LOCAL
        std::string addr_str, local_str;

        auto* payload = static_cast<struct nlattr*>(
            mnl_nlmsg_get_payload_offset(nlh, sizeof(*ifa)));
        auto* payload_tail = static_cast<char*>(mnl_nlmsg_get_payload_tail(nlh));
        size_t payload_size = static_cast<size_t>(
            payload_tail - reinterpret_cast<char*>(payload));

        for (struct nlattr* attr = payload;
             mnl_attr_ok(attr, payload_size);
             attr = mnl_attr_next(attr)) {
            int type = mnl_attr_get_type(attr);
            uint16_t len = mnl_attr_get_payload_len(attr);

            if (type == IFA_ADDRESS) {
                addr_str = format_addr(mnl_attr_get_payload(attr), ifa->ifa_family, len);
            } else if (type == IFA_LOCAL) {
                local_str = format_addr(mnl_attr_get_payload(attr), ifa->ifa_family, len);
            }
        }

        // Prefer IFA_LOCAL over IFA_ADDRESS (point-to-point interfaces)
        const auto& chosen = !local_str.empty() ? local_str : addr_str;
        if (!chosen.empty()) {
            it->second.ipv4_addr = chosen;
        }

        return MNL_CB_OK;
    }

}

NetlinkMonitor::NetlinkMonitor() : fd_(-1), fd_ptr_(nullptr) {}
NetlinkMonitor::~NetlinkMonitor() {
    if (fd_ptr_) {
        mnl_socket_close(fd_ptr_);
    }
}


bool NetlinkMonitor::init() {
    struct mnl_socket* nl = mnl_socket_open(NETLINK_ROUTE);
    if (!nl) return false;

    // Subscribe to link and IPv4 address notifications
    if (mnl_socket_bind(nl, RTMGRP_LINK | RTMGRP_IPV4_IFADDR, MNL_SOCKET_AUTOPID) < 0) {
        mnl_socket_close(nl);
        return false;
    }

    fd_ = mnl_socket_get_fd(nl);
    fd_ptr_ = nl; // Store for the destructor

    // Set non-blocking so drain_events() won't block
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        mnl_socket_close(nl);
        fd_ = -1;
        fd_ptr_ = nullptr;
        return false;
    }

    // Perform initial state dump to populate the cache
    // Order matters: link dump first (populates cache entries), then addr dump
    request_dump();
    request_addr_dump();
    return true;
}

void NetlinkMonitor::request_dump() {
    const size_t buf_size = static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE);
    std::vector<char> buf(buf_size);
    struct nlmsghdr* nlh = mnl_nlmsg_put_header(buf.data());
    nlh->nlmsg_type = RTM_GETLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = time(NULL);

    struct ifinfomsg* ifm = static_cast<struct ifinfomsg*>(mnl_nlmsg_put_extra_header(nlh, sizeof(*ifm)));
    ifm->ifi_family = AF_UNSPEC;

    if (mnl_socket_sendto(static_cast<struct mnl_socket*>(fd_ptr_), nlh, nlh->nlmsg_len) > 0) {
        drain_response();
    }
}

void NetlinkMonitor::request_addr_dump() {
    const size_t buf_size = static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE);
    std::vector<char> buf(buf_size);
    struct nlmsghdr* nlh = mnl_nlmsg_put_header(buf.data());
    nlh->nlmsg_type = RTM_GETADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = time(NULL);

    struct ifaddrmsg* ifa = static_cast<struct ifaddrmsg*>(mnl_nlmsg_put_extra_header(nlh, sizeof(*ifa)));
    ifa->ifa_family = AF_INET;

    if (mnl_socket_sendto(static_cast<struct mnl_socket*>(fd_ptr_), nlh, nlh->nlmsg_len) > 0) {
        drain_response();
    }
}

void NetlinkMonitor::drain_response() {
    const size_t buf_size = static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE);
    std::vector<char> buf(buf_size);

    for (;;) {
        // Socket is non-blocking; wait for data with a short timeout
        struct pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
        if (poll(&pfd, 1, 100) <= 0) break;

        int ret = mnl_socket_recvfrom(static_cast<struct mnl_socket*>(fd_ptr_), buf.data(), buf.size());
        if (ret <= 0) break;

        // Walk all messages in this recv buffer
        auto* nlh = reinterpret_cast<struct nlmsghdr*>(buf.data());
        for (; mnl_nlmsg_ok(nlh, ret); nlh = mnl_nlmsg_next(nlh, &ret)) {
            if (nlh->nlmsg_type == NLMSG_DONE)  return;
            if (nlh->nlmsg_type == NLMSG_ERROR) return;

            switch (nlh->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                data_cb(nlh, &cache_);
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                addr_data_cb(nlh, &cache_);
                break;
            default:
                break;
            }
        }
    }
}

void NetlinkMonitor::process_incoming() {
    const size_t buf_size = static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE);
    std::vector<char> buf(buf_size);
    int ret = mnl_socket_recvfrom(static_cast<struct mnl_socket*>(fd_ptr_), buf.data(), buf.size());
    if (ret <= 0) return;

    // Dispatch by message type — socket now receives both link and addr events
    auto* nlh = reinterpret_cast<struct nlmsghdr*>(buf.data());
    for (; mnl_nlmsg_ok(nlh, ret); nlh = mnl_nlmsg_next(nlh, &ret)) {
        switch (nlh->nlmsg_type) {
        case RTM_NEWLINK:
        case RTM_DELLINK:
            data_cb(nlh, &cache_);
            break;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            addr_data_cb(nlh, &cache_);
            break;
        default:
            break;
        }
    }
}

void NetlinkMonitor::drain_events() {
    const size_t buf_size = static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE);
    std::vector<char> buf(buf_size);

    for (;;) {
        int ret = mnl_socket_recvfrom(static_cast<struct mnl_socket*>(fd_ptr_), buf.data(), buf.size());
        if (ret <= 0) break;  // EAGAIN/EWOULDBLOCK on non-blocking socket

        auto* nlh = reinterpret_cast<struct nlmsghdr*>(buf.data());
        for (; mnl_nlmsg_ok(nlh, ret); nlh = mnl_nlmsg_next(nlh, &ret)) {
            switch (nlh->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                data_cb(nlh, &cache_);
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                addr_data_cb(nlh, &cache_);
                break;
            default:
                break;
            }
        }
    }
}

std::vector<NetlinkInterfaceStats> NetlinkMonitor::get_all_stats() const {
    std::vector<NetlinkInterfaceStats> result;
    for (const auto& [index, stats] : cache_) {
        result.push_back(stats);
    }
    return result;
}

} // namespace edge

#else

// Fallback stub when EDGE_HAS_LIBMNL is not defined
namespace edge {

std::vector<NetlinkInterfaceStats> query_netlink_stats() {
    return {};
}

} // namespace edge

#endif
