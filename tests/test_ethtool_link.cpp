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
