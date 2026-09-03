/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "pair_record.hpp"

namespace {
const char *kPairedFile = MOONLIGHT_3DS_PATH "/paired";
const char *kHostProfilesFile = MOONLIGHT_3DS_PATH "/host_profiles";
const char *kLastHostFile = MOONLIGHT_3DS_PATH "/last_host";
const char *kKeysDir = MOONLIGHT_3DS_PATH "/keys";
const char *kDiagnosticsDir = MOONLIGHT_3DS_PATH "/diagnostics";

void ensure_dir(const char *path) {
    struct stat st {};
    if (stat(path, &st) != 0) {
        mkdir(path, 0777);
    }
}

void ensure_moonlight_sd_dirs() {
    ensure_dir(MOONLIGHT_3DS_PATH);
    ensure_dir(kKeysDir);
    ensure_dir(kDiagnosticsDir);
}

std::string make_host_key(const std::string &address, uint16_t port) {
    return address + ":" + std::to_string(port);
}

std::vector<std::pair<std::string, std::string>> read_host_profiles() {
    std::vector<std::pair<std::string, std::string>> profiles;
    std::ifstream file(kHostProfilesFile);
    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty()) {
            continue;
        }

        const auto delimiter = line.find('|');
        if (delimiter == std::string::npos) {
            continue;
        }

        std::string host = line.substr(0, delimiter);
        std::string profile = line.substr(delimiter + 1);
        trim(host);
        trim(profile);
        if (!host.empty() && !profile.empty()) {
            profiles.emplace_back(host, profile);
        }
    }
    return profiles;
}

void write_host_profiles(
    const std::vector<std::pair<std::string, std::string>> &profiles) {
    FILE *fd = fopen(kHostProfilesFile, "w");
    if (fd == NULL) {
        return;
    }

    for (const auto &entry : profiles) {
        fprintf(fd, "%s|%s\n", entry.first.c_str(), entry.second.c_str());
    }
    fclose(fd);
}
} // namespace

inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
}

inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            s.end());
}

void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

void add_pair_address(std::string address, uint16_t port) {
    ensure_moonlight_sd_dirs();
    address += ":" + std::to_string(port);

    auto address_list = list_paired_addresses();
    for (auto entry : address_list) {
        if (entry == address) {
            return;
        }
    }
    address_list.push_back(address);

    remove(kPairedFile);

    FILE *fd = fopen(kPairedFile, "w");
    if (fd == NULL) {
        return;
    }
    for (auto addr_string : address_list) {
        trim(addr_string);
        fprintf(fd, "%s\n", addr_string.c_str());
    }
    fclose(fd);
}

void remove_pair_address(std::string address, uint16_t port) {
    const std::string raw_address = address;
    address += ":" + std::to_string(port);

    auto address_list = list_paired_addresses();

    remove(kPairedFile);

    FILE *fd = fopen(kPairedFile, "w");
    if (fd != NULL) {
        for (auto addr_string : address_list) {
            if (addr_string != address) {
                trim(addr_string);
                fprintf(fd, "%s\n", addr_string.c_str());
            }
        }
        fclose(fd);
    }

    remove_host_profile(raw_address, port);
}

std::vector<std::string> list_paired_addresses() {
    std::vector<std::string> addresses = std::vector<std::string>();
    std::ifstream pair_file(kPairedFile);
    std::string line;
    while (std::getline(pair_file, line)) {
        trim(line);
        if (!line.empty()) {
            addresses.push_back(line);
        }
    }
    return addresses;
}

std::string get_host_profile(const std::string &address, uint16_t port) {
    const std::string key = make_host_key(address, port);
    for (const auto &entry : read_host_profiles()) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return "";
}

void set_host_profile(const std::string &address, uint16_t port,
                      const std::string &profile) {
    ensure_moonlight_sd_dirs();
    const std::string key = make_host_key(address, port);
    auto profiles = read_host_profiles();
    for (auto &entry : profiles) {
        if (entry.first == key) {
            entry.second = profile;
            write_host_profiles(profiles);
            return;
        }
    }

    profiles.emplace_back(key, profile);
    write_host_profiles(profiles);
}

void remove_host_profile(const std::string &address, uint16_t port) {
    const std::string key = make_host_key(address, port);
    auto profiles = read_host_profiles();
    profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                  [&key](const auto &entry) {
                                      return entry.first == key;
                                  }),
                   profiles.end());
    write_host_profiles(profiles);
}

void set_last_host(const std::string &address, uint16_t port) {
    ensure_moonlight_sd_dirs();
    FILE *fd = fopen(kLastHostFile, "w");
    if (fd == NULL) {
        return;
    }
    fprintf(fd, "%s:%u\n", address.c_str(), static_cast<unsigned>(port));
    fclose(fd);
}

bool get_last_host(std::string &address, uint16_t &port) {
    std::ifstream file(kLastHostFile);
    std::string line;
    if (!std::getline(file, line)) {
        return false;
    }
    trim(line);
    if (line.empty()) {
        return false;
    }

    const auto colon = line.rfind(':');
    if (colon == std::string::npos) {
        address = line;
        port = 47989;
        return true;
    }

    address = line.substr(0, colon);
    trim(address);
    if (address.empty()) {
        return false;
    }

    try {
        const int parsed_port = std::stoi(line.substr(colon + 1));
        if (parsed_port <= 0 || parsed_port > 65535) {
            return false;
        }
        port = static_cast<uint16_t>(parsed_port);
    } catch (...) {
        return false;
    }
    return true;
}
