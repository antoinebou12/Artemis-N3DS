#include "host_discovery.hpp"

#include "system/pair_record.hpp"

#include <3ds.h>

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
constexpr int kBatchSize = 24;
constexpr long kBatchTimeoutUs = 70000;

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

        const int old_flags = fcntl(fd, F_GETFL, 0);
        if (old_flags >= 0) {
            fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);
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
} // namespace

std::vector<DiscoveredHost> discover_moonlight_hosts() {
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

    in_addr local_addr{};
    in_addr netmask_addr{};
    in_addr broadcast_addr{};
    if (SOCU_GetIPInfo(&local_addr, &netmask_addr, &broadcast_addr) != 0) {
        return hosts;
    }

    const std::uint32_t local_ip = ntohl(local_addr.s_addr);
    const std::uint32_t mask = ntohl(netmask_addr.s_addr);
    std::uint32_t network = local_ip & mask;
    std::uint32_t broadcast = ntohl(broadcast_addr.s_addr);

    if (broadcast <= network + 1) {
        return hosts;
    }

    std::uint32_t first_host = network + 1;
    std::uint32_t last_host = broadcast - 1;

    // Avoid huge scans on unusually broad subnets. Most home networks are /24;
    // on broader networks we scan the 3DS's local /24, which keeps discovery
    // responsive and bounded.
    if (last_host - first_host + 1 > 254) {
        network = local_ip & 0xFFFFFF00u;
        first_host = network + 1;
        last_host = network + 254;
    }

    for (std::uint32_t batch_start = first_host; batch_start <= last_host;) {
        const std::uint32_t batch_end =
            std::min(last_host, batch_start + kBatchSize - 1);
        scan_batch(batch_start, batch_end, local_ip, hosts, seen);
        if (batch_end == UINT32_MAX) {
            break;
        }
        batch_start = batch_end + 1;
    }

    std::stable_sort(hosts.begin(), hosts.end(),
                     [](const DiscoveredHost &a, const DiscoveredHost &b) {
                         if (a.saved != b.saved) {
                             return a.saved > b.saved;
                         }
                         return a.address < b.address;
                     });
    return hosts;
}
