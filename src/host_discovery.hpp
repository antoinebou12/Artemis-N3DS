#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DiscoveredHost {
    std::string address;
    std::uint16_t port = 47989;
    bool saved = false;
};

enum class NetworkStatus {
    Ready,
    NoWifi,
    NoLanIp,
};

NetworkStatus moonlight_network_status();
const char *moonlight_network_status_message(NetworkStatus status);

std::vector<DiscoveredHost> discover_moonlight_hosts(
    bool scan_common_subnets = true);
