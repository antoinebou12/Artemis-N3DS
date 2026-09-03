#pragma once

#include <cstdint>
#include <set>
#include <vector>

constexpr std::uint32_t kMoonlightPreferredHostIp = 0xC0A84432u; // 192.168.68.50
constexpr std::uint32_t kMoonlightPreferredNetwork = 0xC0A84400u; // 192.168.68.0

inline bool moonlight_is_private_slash24(std::uint32_t network) {
    const std::uint32_t first = network & 0xFFFFFF00u;
    if ((first & 0xFF000000u) == 0x0A000000u) {
        return true; // 10.0.0.0/8
    }
    if ((first & 0xFFF00000u) == 0xAC100000u) {
        return true; // 172.16.0.0/12
    }
    if ((first & 0xFFFF0000u) == 0xC0A80000u) {
        return true; // 192.168.0.0/16
    }
    return false;
}

inline void moonlight_add_scan_network(std::vector<std::uint32_t> &networks,
                                        std::set<std::uint32_t> &seen,
                                        std::uint32_t network) {
    network &= 0xFFFFFF00u;
    if (!moonlight_is_private_slash24(network) || !seen.insert(network).second) {
        return;
    }
    networks.push_back(network);
}

// Manual Scan only: the 3DS LAN /24, plus the preferred Moonlight subnet when
// it differs. Wide 192.168/10 sweeps were too slow and missed GameStream hosts
// under a short connect timeout.
inline void moonlight_collect_scan_networks(std::uint32_t local_ip,
                                           std::vector<std::uint32_t> &networks,
                                           bool /*scan_common_subnets*/) {
    std::set<std::uint32_t> seen;
    const std::uint32_t local_net = local_ip & 0xFFFFFF00u;

    moonlight_add_scan_network(networks, seen, local_net);
    moonlight_add_scan_network(networks, seen, kMoonlightPreferredNetwork);
}
