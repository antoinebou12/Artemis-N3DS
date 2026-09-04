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
// Sunshine / NVIDIA GameStream HTTP control port used for serverinfo / pair.
constexpr std::uint16_t kGameStreamHttpPort = 47989;
constexpr int kBatchSize = 8;
// Wi-Fi on 3DS is slow; 60ms batches missed open GameStream listeners.
constexpr long kBulkTimeoutUs = 300000;
constexpr long kPreferredTimeoutUs = 500000;
constexpr int kDiscoverySocketBuffer = 2048;

struct PendingSocket {
    int fd = -1;
    std::uint32_t ip_host_order = 0;
};

bool usable_remote_address(const std::string &address) {
    if (address.empty()) {
        return false;
    }

    // Hostnames remain valid. For IPv4 literals, explicitly reject loopback,
    // unspecified, multicast, and reserved addresses so a stale 127.0.0.1
    // record can never appear as a remote Sunshine PC on the 3DS.
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
        return true;
    }
    return moonlight_is_usable_remote_ipv4(ntohl(parsed.s_addr));
}

bool parse_saved_address(const std::string &entry, std::string &address,
                         std::uint16_t &port) {
    const auto colon = entry.rfind(':');
    if (colon == std::string::npos) {
        address = entry;
        port = kGameStreamHttpPort;
        return usable_remote_address(address);
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
    return usable_remote_address(address);
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

std::string network_label(std::uint32_t network) {
    return ip_to_string(network & 0xFFFFFF00u) + "/24";
}

void append_if_new(std::vector<DiscoveredHost> &hosts,
                   std::set<std::string> &seen, const std::string &address,
                   std::uint16_t port, bool saved) {
    if (!usable_remote_address(address)) {
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
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kDiscoverySocketBuffer,
               sizeof(kDiscoverySocketBuffer));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kDiscoverySocketBuffer,
               sizeof(kDiscoverySocketBuffer));
}

std::vector<std::string> found_lines(const std::vector<DiscoveredHost> &hosts) {
    std::vector<std::string> lines;
    lines.reserve(std::min<std::size_t>(hosts.size(), 5));
    const std::size_t start =
        hosts.size() > 5 ? hosts.size() - 5 : 0;
    for (std::size_t i = start; i < hosts.size(); ++i) {
        const auto &host = hosts[i];
        std::string row = host.saved ? "★ " : "+ ";
        row += host.address + ":" + std::to_string(host.port);
        lines.push_back(row);
    }
    return lines;
}

void report_progress(const DiscoveryProgressFn &on_progress,
                     const char *title, const std::string &status,
                     const std::vector<DiscoveredHost> &hosts) {
    if (!on_progress) {
        return;
    }
    DiscoveryProgress progress;
    progress.title = title;
    progress.status = status;
    progress.lines = found_lines(hosts);
    if (progress.lines.empty()) {
        progress.lines.push_back("Waiting for host replies...");
    }
    on_progress(progress);
}

void probe_ips(const std::vector<std::uint32_t> &ips, std::uint32_t local_ip,
               std::vector<DiscoveredHost> &hosts, std::set<std::string> &seen,
               long timeout_us, const DiscoveryProgressFn &on_progress,
               const char *title, std::size_t &probed_count,
               std::size_t total_estimate) {
    if (ips.empty()) {
        return;
    }
    for (size_t offset = 0; offset < ips.size();) {
        if (!aptMainLoop()) {
            return;
        }
        const size_t end = std::min(ips.size(), offset + kBatchSize);
        std::vector<PendingSocket> pending;
        pending.reserve(kBatchSize);
        fd_set write_fds;
        FD_ZERO(&write_fds);
        int max_fd = -1;
        std::string batch_first;

        for (size_t i = offset; i < end; ++i) {
            const std::uint32_t ip = ips[i];
            if (ip == local_ip || !moonlight_is_usable_remote_ipv4(ip)) {
                continue;
            }
            const std::string address = ip_to_string(ip);
            const std::string seen_key =
                address + ":" + std::to_string(kGameStreamHttpPort);
            if (seen.count(seen_key) != 0) {
                continue;
            }
            if (batch_first.empty()) {
                batch_first = address;
            }

            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                continue;
            }

            configure_probe_socket(fd);
            const int old_flags = fcntl(fd, F_GETFL, 0);
            if (old_flags < 0 || fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
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
                append_if_new(hosts, seen, address, kGameStreamHttpPort, false);
                close(fd);
                continue;
            }

            if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
                close(fd);
                continue;
            }
            if (fd >= FD_SETSIZE) {
                close(fd);
                continue;
            }

            pending.push_back({fd, ip});
            FD_SET(fd, &write_fds);
            max_fd = std::max(max_fd, fd);
        }

        if (!batch_first.empty()) {
            std::string status = "Port " + std::to_string(kGameStreamHttpPort) +
                                 "  " + batch_first;
            if (total_estimate > 0) {
                status += "  (" + std::to_string(probed_count) + "/" +
                          std::to_string(total_estimate) + ")";
            }
            status += "  found " + std::to_string(hosts.size());
            report_progress(on_progress, title, status, hosts);
        }

        if (!pending.empty() && max_fd >= 0) {
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = timeout_us;
            const int ready =
                select(max_fd + 1, nullptr, &write_fds, nullptr, &timeout);
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

        probed_count += (end - offset);
        offset = end;
    }
}

void scan_slash24(std::uint32_t network, std::uint32_t local_ip,
                  std::vector<DiscoveredHost> &hosts,
                  std::set<std::string> &seen, bool priority_only,
                  bool wide_priority, const DiscoveryProgressFn &on_progress,
                  const char *title, std::size_t &probed_count,
                  std::size_t total_estimate) {
    static constexpr std::uint8_t kPriorityHosts[] = {
        55, 1, 50, 68, 100, 254, 2, 10, 20, 80, 101, 150, 200};
    static constexpr std::uint8_t kWidePriorityHosts[] = {55, 1, 50, 100, 254};

    std::vector<std::uint32_t> ips;
    ips.reserve(priority_only ? (wide_priority ? 5 : 13) : 254);
    std::set<std::uint32_t> queued;

    const std::uint8_t *priority = wide_priority ? kWidePriorityHosts
                                                 : kPriorityHosts;
    const std::size_t priority_count =
        wide_priority ? sizeof(kWidePriorityHosts) : sizeof(kPriorityHosts);
    for (std::size_t i = 0; i < priority_count; ++i) {
        const std::uint32_t ip = (network & 0xFFFFFF00u) + priority[i];
        if (queued.insert(ip).second) {
            ips.push_back(ip);
        }
    }
    if (!priority_only) {
        for (std::uint32_t host = 1; host <= 254; ++host) {
            const std::uint32_t ip = (network & 0xFFFFFF00u) + host;
            if (queued.insert(ip).second) {
                ips.push_back(ip);
            }
        }
    }

    report_progress(on_progress, title,
                    "Scan " + network_label(network) + "  found " +
                        std::to_string(hosts.size()),
                    hosts);
    probe_ips(ips, local_ip, hosts, seen, kBulkTimeoutUs, on_progress, title,
              probed_count, total_estimate);
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

std::size_t estimate_probe_total(const std::vector<std::uint32_t> &networks,
                                 std::uint32_t /*local_net*/,
                                 bool /*scan_common_subnets*/) {
    std::size_t total = 2; // preferred + last host
    // Manual Scan always walks every host on each selected /24 for port 47989.
    total += networks.size() * 254;
    return total;
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
        return "No Wi-Fi.\n\nConnect, then Scan.";
    case NetworkStatus::NoLanIp:
        return "No LAN address.\n\nCheck Wi-Fi, then Scan.";
    case NetworkStatus::Ready:
        return "";
    }
    return "";
}

std::vector<DiscoveredHost> list_saved_moonlight_hosts() {
    std::vector<DiscoveredHost> hosts;
    std::set<std::string> seen;
    for (const auto &entry : list_paired_addresses()) {
        std::string address;
        std::uint16_t port = kGameStreamHttpPort;
        if (parse_saved_address(entry, address, port)) {
            append_if_new(hosts, seen, address, port, true);
        }
    }
    return hosts;
}

std::vector<DiscoveredHost> discover_moonlight_hosts(
    bool scan_common_subnets, const DiscoveryProgressFn &on_progress) {
    std::vector<DiscoveredHost> hosts = list_saved_moonlight_hosts();
    std::set<std::string> seen;
    for (const auto &host : hosts) {
        seen.insert(host.address + ":" + std::to_string(host.port));
    }
    (void)scan_common_subnets;
    const char *title = "Scan";

    if (moonlight_network_status() != NetworkStatus::Ready) {
        report_progress(on_progress, title, "Network unavailable", hosts);
        return hosts;
    }

    in_addr local_addr{};
    in_addr netmask_addr{};
    in_addr broadcast_addr{};
    if (SOCU_GetIPInfo(&local_addr, &netmask_addr, &broadcast_addr) != 0) {
        report_progress(on_progress, title, "No LAN address", hosts);
        return hosts;
    }

    const std::uint32_t local_ip = ntohl(local_addr.s_addr);
    const std::uint32_t local_net = local_ip & 0xFFFFFF00u;
    std::size_t probed_count = 0;

    std::vector<std::uint32_t> networks;
    moonlight_collect_scan_networks(local_ip, networks, true);

    std::vector<std::uint32_t> first_ips;
    std::string last_address;
    std::uint16_t last_port = kGameStreamHttpPort;
    if (get_last_host(last_address, last_port)) {
        const std::uint32_t last_ip = ipv4_sort_key(last_address);
        if (last_ip != UINT32_MAX && moonlight_is_usable_remote_ipv4(last_ip)) {
            first_ips.push_back(last_ip);
            moonlight_add_scan_network_from_ip(networks, last_ip);
        }
    }

    const std::size_t total_estimate =
        estimate_probe_total(networks, local_net, true);

    report_progress(on_progress, title,
                    "Probe GameStream port " +
                        std::to_string(kGameStreamHttpPort),
                    hosts);

    if (!first_ips.empty()) {
        probe_ips(first_ips, local_ip, hosts, seen, kPreferredTimeoutUs,
                  on_progress, title, probed_count, total_estimate);
    }

    // Full /24 TCP connect to GameStream HTTP only (no other ports / subnets).
    for (const std::uint32_t network : networks) {
        scan_slash24(network, local_ip, hosts, seen, false, false, on_progress,
                     title, probed_count, total_estimate);
    }

    report_progress(on_progress, title,
                    "Done  found " + std::to_string(hosts.size()), hosts);
    svcSleepThread(100000000LL);

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
