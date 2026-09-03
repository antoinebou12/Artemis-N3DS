#pragma once

#include <cstdint>
#include <set>
#include <vector>

// LAN prefixes probed during a full Scan. Auto-refresh only uses the 3DS
// local /24 plus neighboring /24s so the host list does not freeze.
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

inline void moonlight_collect_scan_networks(std::uint32_t local_ip,
                                           std::vector<std::uint32_t> &networks,
                                           bool scan_common_subnets) {
    std::set<std::uint32_t> seen;
    const std::uint32_t local_net = local_ip & 0xFFFFFF00u;
    moonlight_add_scan_network(networks, seen, local_net);

    for (int delta = -2; delta <= 2; ++delta) {
        if (delta == 0) {
            continue;
        }
        moonlight_add_scan_network(
            networks, seen,
            local_net + static_cast<std::uint32_t>(delta * 256));
    }

    if (!scan_common_subnets) {
        return;
    }

    static constexpr std::uint32_t kCommonSlash24[] = {
        0x0A000000u, // 10.0.0.0
        0x0A000100u, // 10.0.1.0
        0x0A010000u, // 10.1.0.0
        0x0A010100u, // 10.1.1.0
        0x0A0A0000u, // 10.10.0.0
        0x0A0A0A00u, // 10.10.10.0
        0xAC100000u, // 172.16.0.0
        0xAC100100u, // 172.16.1.0
        0xC0A80000u, // 192.168.0.0
        0xC0A80100u, // 192.168.1.0
        0xC0A80200u, // 192.168.2.0
        0xC0A80300u, // 192.168.3.0
        0xC0A80400u, // 192.168.4.0
        0xC0A80500u, // 192.168.5.0
        0xC0A80800u, // 192.168.8.0
        0xC0A80A00u, // 192.168.10.0
        0xC0A83200u, // 192.168.50.0
        0xC0A84400u, // 192.168.68.0
        0xC0A85600u, // 192.168.86.0
        0xC0A86400u, // 192.168.100.0
        0xC0A8B200u, // 192.168.178.0
        0xC0A8FE00u, // 192.168.254.0
    };
    for (const std::uint32_t network : kCommonSlash24) {
        moonlight_add_scan_network(networks, seen, network);
    }
}
