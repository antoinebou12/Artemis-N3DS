#pragma once

#include <cstdint>
#include <functional>
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

struct DiscoveryProgress {
    std::string title;
    std::string status;
    std::vector<std::string> lines;
};

using DiscoveryProgressFn = std::function<void(const DiscoveryProgress &)>;

NetworkStatus moonlight_network_status();
const char *moonlight_network_status_message(NetworkStatus status);

// Saved/paired hosts only — no LAN probing. Call discover_moonlight_hosts()
// only when the user taps Scan (TCP GameStream HTTP port 47989).
std::vector<DiscoveredHost> list_saved_moonlight_hosts();

std::vector<DiscoveredHost> discover_moonlight_hosts(
    bool scan_common_subnets = true,
    const DiscoveryProgressFn &on_progress = {});
