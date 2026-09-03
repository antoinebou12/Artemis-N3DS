#include "app_list_cache.hpp"

#include "system/pair_record.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

namespace {
const char *kApplistDir = MOONLIGHT_3DS_PATH "/applist";

void ensure_dir(const char *path) {
    struct stat st {};
    if (stat(path, &st) != 0) {
        mkdir(path, 0777);
    }
}

std::string safe_host_token(const std::string &address, std::uint16_t port) {
    std::string token = address + "_" + std::to_string(port);
    for (char &ch : token) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || ch == '.' || ch == '-') {
            continue;
        }
        ch = '_';
    }
    return token;
}

std::string cache_path(const std::string &address, std::uint16_t port) {
    return std::string(kApplistDir) + "/" + safe_host_token(address, port) +
           ".txt";
}

std::string ascii_lower(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}
} // namespace

void sort_cached_app_list(std::vector<CachedRemoteApp> &apps) {
    std::sort(apps.begin(), apps.end(),
              [](const CachedRemoteApp &a, const CachedRemoteApp &b) {
                  const std::string la = ascii_lower(a.name);
                  const std::string lb = ascii_lower(b.name);
                  if (la != lb) {
                      return la < lb;
                  }
                  return a.id < b.id;
              });
}

bool app_name_matches_filter(const std::string &name,
                             const std::string &filter) {
    if (filter.empty()) {
        return true;
    }
    const std::string hay = ascii_lower(name);
    const std::string needle = ascii_lower(filter);
    return hay.find(needle) != std::string::npos;
}

bool load_cached_app_list(const std::string &address, std::uint16_t port,
                          std::vector<CachedRemoteApp> &apps) {
    apps.clear();
    std::ifstream file(cache_path(address, port));
    if (!file) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto bar = line.find('|');
        if (bar == std::string::npos) {
            continue;
        }
        CachedRemoteApp app;
        try {
            app.id = std::stoi(line.substr(0, bar));
        } catch (...) {
            continue;
        }
        app.name = line.substr(bar + 1);
        trim(app.name);
        if (app.id > 0 && !app.name.empty()) {
            apps.push_back(std::move(app));
        }
    }

    sort_cached_app_list(apps);
    return !apps.empty();
}

bool save_cached_app_list(const std::string &address, std::uint16_t port,
                          const std::vector<CachedRemoteApp> &apps) {
    if (apps.empty()) {
        return false;
    }
    ensure_dir(MOONLIGHT_3DS_PATH);
    ensure_dir(kApplistDir);

    FILE *fd = std::fopen(cache_path(address, port).c_str(), "w");
    if (fd == nullptr) {
        return false;
    }
    std::fprintf(fd, "# Artemis app list cache\n");
    for (const auto &app : apps) {
        if (app.id <= 0 || app.name.empty()) {
            continue;
        }
        std::fprintf(fd, "%d|%s\n", app.id, app.name.c_str());
    }
    std::fclose(fd);
    return true;
}
