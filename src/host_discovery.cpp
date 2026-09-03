#include "host_discovery.hpp"
#include "host_discovery_scan.hpp"

#include "system/pair_record.hpp"

#include <3ds.h>
#include <3ds/services/ac.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::uint16_t kGameStreamHttpPort = 47989;
// A /24 now completes in at most six short select windows instead of eleven.
// Keep this below FD_SETSIZE and small enough for the 3DS socket service.
constexpr int kBatchSize = 48;
constexpr long kBatchTimeoutUs = 40000;
constexpr int kDiscoverySocketBuffer = 2048;

struct PendingSocket {
    int fd = -1;
    std::uint32_t ip_host_order = 0;
};

bool parse_saved_address(const std::string &entry, std::string &address,
                         std::uint16_t &port) {
    const auto colon = entry.rfind(':');
    if (colon == std::string::npos) {
        address = entry;
        port = kGameStreamHttpPort;
        return !address.empty();
    }

    address = entry.substr(0, colon);
    try {
        const int parsed_port = std::stoi(entry.substr(colon + 1));
        if (parsed_port <= 0 || parsed_port > 65535) {
            return false;
        }
        port = static_cast<std::uint16_t>(parsed_port);
    } catch (...) {
        return false;
    }
    return !address.empty();
}

std::string ip_to_string(std::uint32_t ip_host_order) {
    in_addr addr{};
    addr.s_addr = htonl(ip_host_order);
    char buffer[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        return "";
    }
    return buffer;
}

void append_if_new(std::vector<DiscoveredHost> &hosts,
                   std::set<std::string> &seen, const std::string &address,
                   std::uint16_t port, bool saved) {
    if (address.empty()) {
        return;
    }

    const std::string key = address + ":" + std::to_string(port);
    const auto inserted = seen.insert(key).second;
    if (inserted) {
        hosts.push_back({address, port, saved});
        return;
    }

    if (saved) {
        for (auto &host : hosts) {
            if (host.address == address && host.port == port) {
                host.saved = true;
                break;
            }
        }
    }
}

void configure_probe_socket(int fd) {
    // Discovery sockets only carry a TCP SYN. Tiny buffers reduce SOC memory
    // pressure when dozens of probes are in flight at once.
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kDiscoverySocketBuffer,
               sizeof(kDiscoverySocketBuffer));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kDiscoverySocketBuffer,
               sizeof(kDiscoverySocketBuffer));
}

void scan_batch(std::uint32_t first_ip, std::uint32_t last_ip,
                std::uint32_t local_ip, std::vector<DiscoveredHost> &hosts,
                std::set<std::string> &seen);

void scan_slash24(std::uint32_t network, std::uint32_t local_ip,
                  std::vector<DiscoveredHost> &hosts,
                  std::set<std::string> &seen) {
    const std::uint32_t first_host = network + 1;
    const std::uint32_t last_host = network + 254;
    for (std::uint32_t batch_start = first_host; batch_start <= last_host;) {
        if (!aptMainLoop()) {
            return;
        }
        const std::uint32_t batch_end =
            std::min(last_host, batch_start + kBatchSize - 1);
        scan_batch(batch_start, batch_end, local_ip, hosts, seen);
        if (batch_end == UINT32_MAX) {
            break;
        }
        batch_start = batch_end + 1;
    }
}

void scan_batch(std::uint32_t first_ip, std::uint32_t last_ip,
                std::uint32_t local_ip, std::vector<DiscoveredHost> &hosts,
                std::set<std::string> &seen) {
    std::vector<PendingSocket> pending;
    pending.reserve(kBatchSize);
    fd_set write_fds;
    FD_ZERO(&write_fds);
    int max_fd = -1;

    for (std::uint32_t ip = first_ip; ip <= last_ip; ++ip) {
        if (ip == local_ip) {
            continue;
        }

        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            continue;
        }

        configure_probe_socket(fd);

        const int old_flags = fcntl(fd, F_GETFL, 0);
        if (old_flags < 0 || fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
            // Never allow discovery to fall back to a blocking connect.
            close(fd);
            continue;
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(kGameStreamHttpPort);
        target.sin_addr.s_addr = htonl(ip);

        const int result = connect(fd, reinterpret_cast<sockaddr *>(&target),
                                   sizeof(target));
        if (result == 0) {
            append_if_new(hosts, seen, ip_to_string(ip), kGameStreamHttpPort,
                          false);
            close(fd);
            continue;
        }

        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            close(fd);
            continue;
        }

        // select() cannot represent descriptors outside FD_SETSIZE.
        if (fd >= FD_SETSIZE) {
            close(fd);
            continue;
        }

        pending.push_back({fd, ip});
        FD_SET(fd, &write_fds);
        max_fd = std::max(max_fd, fd);
    }

    if (pending.empty() || max_fd < 0) {
        return;
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = kBatchTimeoutUs;
    const int ready = select(max_fd + 1, nullptr, &write_fds, nullptr, &timeout);

    if (ready > 0) {
        for (const auto &pending_socket : pending) {
            if (!FD_ISSET(pending_socket.fd, &write_fds)) {
                continue;
            }

            int socket_error = 0;
            socklen_t length = sizeof(socket_error);
            if (getsockopt(pending_socket.fd, SOL_SOCKET, SO_ERROR,
                           &socket_error, &length) == 0 &&
                socket_error == 0) {
                append_if_new(hosts, seen,
                              ip_to_string(pending_socket.ip_host_order),
                              kGameStreamHttpPort, false);
            }
        }
    }

    for (const auto &pending_socket : pending) {
        close(pending_socket.fd);
    }
}

std::uint32_t ipv4_sort_key(const std::string &address) {
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
        return UINT32_MAX;
    }
    return ntohl(parsed.s_addr);
}

bool wifi_connected() {
    u32 status = 0;
    if (R_FAILED(ACU_GetStatus(&status))) {
        return false;
    }
    return status == 3;
}
} // namespace

NetworkStatus moonlight_network_status() {
    if (!wifi_connected()) {
        return NetworkStatus::NoWifi;
    }

    in_addr local_addr{};
    in_addr netmask_addr{};
    in_addr broadcast_addr{};
    if (SOCU_GetIPInfo(&local_addr, &netmask_addr, &broadcast_addr) != 0) {
        return NetworkStatus::NoLanIp;
    }

    if (local_addr.s_addr == 0 || broadcast_addr.s_addr == 0) {
        return NetworkStatus::NoLanIp;
    }

    return NetworkStatus::Ready;
}

const char *moonlight_network_status_message(NetworkStatus status) {
    switch (status) {
    case NetworkStatus::NoWifi:
        return "No Wi-Fi connection.\n\nConnect in System Settings, then "
               "return and press Refresh.";
    case NetworkStatus::NoLanIp:
        return "Wi-Fi is on but this 3DS has no LAN address.\n\nCheck your "
               "router or hotspot, then press Refresh.";
    case NetworkStatus::Ready:
        return "";
    }
    return "";
}

std::vector<DiscoveredHost> discover_moonlight_hosts(bool scan_common_subnets) {
    std::vector<DiscoveredHost> hosts;
    std::set<std::string> seen;

    // Saved hosts are always visible even when discovery is unavailable.
    for (const auto &entry : list_paired_addresses()) {
        std::string address;
        std::uint16_t port = kGameStreamHttpPort;
        if (parse_saved_address(entry, address, port)) {
            append_if_new(hosts, seen, address, port, true);
        }
    }

    if (moonlight_network_status() != NetworkStatus::Ready) {
        return hosts;
    }

    in_addr local_addr{};
    in_addr netmask_addr{};
    in_addr broadcast_addr{};
    if (SOCU_GetIPInfo(&local_addr, &netmask_addr, &broadcast_addr) != 0) {
        return hosts;
    }

    const std::uint32_t local_ip = ntohl(local_addr.s_addr);
    std::vector<std::uint32_t> networks;
    moonlight_collect_scan_networks(local_ip, networks, scan_common_subnets);
    for (const std::uint32_t network : networks) {
        scan_slash24(network, local_ip, hosts, seen);
    }

    std::stable_sort(hosts.begin(), hosts.end(),
                     [](const DiscoveredHost &a, const DiscoveredHost &b) {
                         if (a.saved != b.saved) {
                             return a.saved > b.saved;
                         }
                         const auto a_ip = ipv4_sort_key(a.address);
                         const auto b_ip = ipv4_sort_key(b.address);
                         if (a_ip != b_ip) {
                             return a_ip < b_ip;
                         }
                         if (a.address != b.address) {
                             return a.address < b.address;
                         }
                         return a.port < b.port;
                     });
    return hosts;
}
