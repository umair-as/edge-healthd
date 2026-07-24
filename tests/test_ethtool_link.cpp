// SPDX-License-Identifier: MIT
// Smoke tests for the ethtool generic-netlink link query. These exercise the
// graceful-degradation paths without asserting a specific speed/duplex (which
// depends on the host's NICs); the live value is verified end-to-end in the
// device gate.

#include <catch2/catch_test_macros.hpp>

#include "ethtool_link.hpp"

using namespace edge;

TEST_CASE("ethtool_query_links: empty input yields empty map", "[ethtool]") {
    auto m = ethtool_query_links({});
    CHECK(m.empty());
}

TEST_CASE("ethtool_query_links: bogus interface degrades gracefully", "[ethtool]") {
    // A non-existent interface must not crash and must not appear in the result.
    auto m = ethtool_query_links({"edge_no_such_if0"});
    CHECK(m.find("edge_no_such_if0") == m.end());
}

TEST_CASE("ethtool_query_links: loopback reports no ethtool link", "[ethtool]") {
    // Loopback has no ethtool link settings, so it should carry no speed/duplex
    // (either absent from the map or with unset fields). Must not throw.
    auto m = ethtool_query_links({"lo"});
    if (auto it = m.find("lo"); it != m.end()) {
        CHECK_FALSE(it->second.speed_mbps.has_value());
        CHECK_FALSE(it->second.duplex.has_value());
    }
    SUCCEED();
}

TEST_CASE("EthtoolQuerier: reuse across cycles is stable", "[ethtool]") {
    // The persistent querier caches the socket + family id after the first
    // query and reuses them. Repeated queries must stay consistent and never
    // throw — this is the steady-state daemon path (issue #59).
    EthtoolQuerier q;
    const std::vector<std::string> ifs{"lo", "edge_no_such_if0"};

    auto first = q.query(ifs);
    auto second = q.query(ifs); // second cycle hits the cached family + socket

    // A bogus interface never appears; loopback carries no speed/duplex.
    CHECK(first.find("edge_no_such_if0") == first.end());
    CHECK(second.find("edge_no_such_if0") == second.end());
    for (const auto* m : {&first, &second}) {
        if (auto it = m->find("lo"); it != m->end()) {
            CHECK_FALSE(it->second.speed_mbps.has_value());
            CHECK_FALSE(it->second.duplex.has_value());
        }
    }
    SUCCEED();
}

TEST_CASE("EthtoolQuerier: empty input yields empty map", "[ethtool]") {
    // An empty batch must short-circuit before touching the socket, on both a
    // fresh querier and one that has already primed its socket.
    EthtoolQuerier q;
    CHECK(q.query({}).empty());
    (void)q.query({"lo"}); // prime the socket/family
    CHECK(q.query({}).empty());
}
