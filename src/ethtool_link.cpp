// SPDX-License-Identifier: MIT
// edge-healthd: ethtool generic-netlink link-settings query

#include "ethtool_link.hpp"

#include <vector>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>
#include <linux/ethtool_netlink.h>
#include <linux/ethtool.h>   // SPEED_UNKNOWN, DUPLEX_*

namespace edge {

namespace {

// Outcome of one request/reply exchange. A per-message error (the kernel
// answering NLMSG_ERROR, e.g. an interface with no ethtool ops) is distinct
// from a socket-level error: the former just skips that interface, the latter
// means the persistent socket must be torn down and reopened.
enum class ReqStatus { Ok, MsgError, SocketError };

unsigned int next_seq() {
    // Uniqueness within the socket is all mnl_cb_run needs for matching.
    static unsigned int seq = 0;
    return ++seq;
}

// --- Attribute / message callbacks (C-style, as libmnl expects) -------------

int family_id_attr_cb(const struct nlattr* attr, void* data) {
    if (mnl_attr_get_type(attr) == CTRL_ATTR_FAMILY_ID &&
        mnl_attr_validate(attr, MNL_TYPE_U16) >= 0) {
        *static_cast<uint16_t*>(data) = mnl_attr_get_u16(attr);
    }
    return MNL_CB_OK;
}

int family_msg_cb(const struct nlmsghdr* nlh, void* data) {
    mnl_attr_parse(nlh, sizeof(struct genlmsghdr), family_id_attr_cb, data);
    return MNL_CB_OK;
}

int linkmodes_attr_cb(const struct nlattr* attr, void* data) {
    auto* info = static_cast<EthtoolLink*>(data);
    switch (mnl_attr_get_type(attr)) {
        case ETHTOOL_A_LINKMODES_SPEED:
            if (mnl_attr_validate(attr, MNL_TYPE_U32) >= 0) {
                const uint32_t sp = mnl_attr_get_u32(attr);
                // 0 = no link; SPEED_UNKNOWN is (__u32)-1.
                if (sp != 0 && sp != static_cast<uint32_t>(SPEED_UNKNOWN)) {
                    info->speed_mbps = sp;
                }
            }
            break;
        case ETHTOOL_A_LINKMODES_DUPLEX:
            if (mnl_attr_validate(attr, MNL_TYPE_U8) >= 0) {
                switch (mnl_attr_get_u8(attr)) {
                    case DUPLEX_FULL: info->duplex = Duplex::Full; break;
                    case DUPLEX_HALF: info->duplex = Duplex::Half; break;
                    default: break; // DUPLEX_UNKNOWN → leave nullopt
                }
            }
            break;
        default:
            break;
    }
    return MNL_CB_OK;
}

int linkmodes_msg_cb(const struct nlmsghdr* nlh, void* data) {
    mnl_attr_parse(nlh, sizeof(struct genlmsghdr), linkmodes_attr_cb, data);
    return MNL_CB_OK;
}

// Run a request/reply exchange, dispatching each reply message through `cb`.
// SocketError means the send/recv failed (socket is suspect); MsgError means
// the kernel replied NLMSG_ERROR for this request; Ok otherwise.
ReqStatus run_request(struct mnl_socket* nl, struct nlmsghdr* nlh,
                      unsigned int seq, unsigned int portid, mnl_cb_t cb,
                      void* data) {
    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
        return ReqStatus::SocketError;
    }
    std::vector<char> buf(static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE));
    ssize_t n = mnl_socket_recvfrom(nl, buf.data(), buf.size());
    if (n < 0) {
        return ReqStatus::SocketError;
    }
    while (n > 0) {
        const int ret = mnl_cb_run(buf.data(), static_cast<size_t>(n), seq,
                                   portid, cb, data);
        if (ret <= MNL_CB_STOP) {
            return ret == MNL_CB_ERROR ? ReqStatus::MsgError : ReqStatus::Ok;
        }
        n = mnl_socket_recvfrom(nl, buf.data(), buf.size());
        if (n < 0) {
            return ReqStatus::SocketError;
        }
    }
    return ReqStatus::Ok;
}

uint16_t resolve_family(struct mnl_socket* nl, unsigned int portid,
                        const char* name) {
    std::vector<char> buf(static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE));
    struct nlmsghdr* nlh = mnl_nlmsg_put_header(buf.data());
    nlh->nlmsg_type = GENL_ID_CTRL;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    const unsigned int seq = next_seq();
    nlh->nlmsg_seq = seq;

    auto* genl = static_cast<struct genlmsghdr*>(
        mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr)));
    genl->cmd = CTRL_CMD_GETFAMILY;
    genl->version = 1;
    mnl_attr_put_u16(nlh, CTRL_ATTR_FAMILY_ID, GENL_ID_CTRL);
    mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, name);

    uint16_t family = 0;
    run_request(nl, nlh, seq, portid, family_msg_cb, &family);
    return family;
}

// Query one interface. `status` reports whether a failure was per-message
// (skip this interface) or socket-level (the caller resets the socket).
std::optional<EthtoolLink> query_one(struct mnl_socket* nl, unsigned int portid,
                                     uint16_t family, const std::string& ifname,
                                     ReqStatus& status) {
    std::vector<char> buf(static_cast<size_t>(MNL_SOCKET_BUFFER_SIZE));
    struct nlmsghdr* nlh = mnl_nlmsg_put_header(buf.data());
    nlh->nlmsg_type = family;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    const unsigned int seq = next_seq();
    nlh->nlmsg_seq = seq;

    auto* genl = static_cast<struct genlmsghdr*>(
        mnl_nlmsg_put_extra_header(nlh, sizeof(struct genlmsghdr)));
    genl->cmd = ETHTOOL_MSG_LINKMODES_GET;
    genl->version = ETHTOOL_GENL_VERSION;

    struct nlattr* nest = mnl_attr_nest_start(nlh, ETHTOOL_A_LINKMODES_HEADER);
    mnl_attr_put_strz(nlh, ETHTOOL_A_HEADER_DEV_NAME, ifname.c_str());
    mnl_attr_nest_end(nlh, nest);

    EthtoolLink info;
    status = run_request(nl, nlh, seq, portid, linkmodes_msg_cb, &info);
    if (status != ReqStatus::Ok) {
        return std::nullopt; // e.g. interface has no ethtool ops (MsgError)
    }
    return info;
}

} // namespace

EthtoolQuerier::~EthtoolQuerier() { reset(); }

void EthtoolQuerier::reset() {
    if (sock_) {
        mnl_socket_close(sock_);
    }
    sock_ = nullptr;
    portid_ = 0;
    family_ = 0;
}

bool EthtoolQuerier::ensure_ready() {
    if (!sock_) {
        sock_ = mnl_socket_open(NETLINK_GENERIC);
        if (!sock_) {
            return false; // no genl access → speed/duplex simply absent
        }
        if (mnl_socket_bind(sock_, 0, MNL_SOCKET_AUTOPID) < 0) {
            reset();
            return false;
        }
        portid_ = mnl_socket_get_portid(sock_);
    }
    if (family_ == 0) {
        // Resolve once and cache; a bound-but-unresolved socket is kept so only
        // the resolve is retried next cycle.
        family_ = resolve_family(sock_, portid_, ETHTOOL_GENL_NAME);
        if (family_ == 0) {
            return false; // ethtool family unavailable on this kernel
        }
    }
    return true;
}

std::unordered_map<std::string, EthtoolLink>
EthtoolQuerier::query(const std::vector<std::string>& ifnames) {
    std::unordered_map<std::string, EthtoolLink> out;
    if (ifnames.empty()) return out;
    if (!ensure_ready()) return out;

    for (const auto& ifname : ifnames) {
        ReqStatus status = ReqStatus::Ok;
        auto info = query_one(sock_, portid_, family_, ifname, status);
        if (status == ReqStatus::SocketError) {
            // The persistent socket is suspect; drop it so the next cycle
            // reopens cleanly and abandon the rest of this batch.
            reset();
            break;
        }
        if (info && (info->speed_mbps || info->duplex)) {
            out[ifname] = *info;
        }
    }
    return out;
}

std::unordered_map<std::string, EthtoolLink>
ethtool_query_links(const std::vector<std::string>& ifnames) {
    EthtoolQuerier querier;
    return querier.query(ifnames);
}

} // namespace edge
