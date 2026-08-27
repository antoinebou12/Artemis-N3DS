#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DiscoveredHost {
    std::string address;
    std::uint16_t port = 47989;
    bool saved = false;
};

std::vector<DiscoveredHost> discover_moonlight_hosts();
