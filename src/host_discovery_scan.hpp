#pragma once

#include <cstdint>
#include <set>
#include <vector>

inline bool moonlight_is_usable_remote_ipv4(std::uint32_t ip_host_order) {
    const std::uint8_t first = static_cast<std::uint8_t>(ip_host_order >> 24);
    if (ip_host_order == 0 || ip_host_order == 0xFFFFFFFFu) {
        return false;
    }
    if (first == 0 || first == 127) {
        return false; // unspecified / loopback are never remote Moonlight hosts
    }
    if (first >= 224) {
        return false; // multicast / reserved
    }
    return true;
}

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

// Adds the /24 that an individual address belongs to (last host), so a PC on
// a neighbouring subnet still gets swept without hardcoding a preferred LAN.
inline void moonlight_add_scan_network_from_ip(
    std::vector<std::uint32_t> &networks, std::uint32_t ip) {
    if (!moonlight_is_usable_remote_ipv4(ip)) {
        return;
    }
    const std::uint32_t network = ip & 0xFFFFFF00u;
    if (!moonlight_is_private_slash24(network)) {
        return;
    }
    for (const std::uint32_t existing : networks) {
        if (existing == network) {
            return;
        }
    }
    networks.push_back(network);
}

// Manual Scan: only the 3DS LAN /24. Extra subnets come from last host via
// moonlight_add_scan_network_from_ip — never a hardcoded preferred LAN IP.
inline void moonlight_collect_scan_networks(std::uint32_t local_ip,
                                           std::vector<std::uint32_t> &networks,
                                           bool /*scan_common_subnets*/) {
    std::set<std::uint32_t> seen;
    const std::uint32_t local_net = local_ip & 0xFFFFFF00u;

    moonlight_add_scan_network(networks, seen, local_net);
}
