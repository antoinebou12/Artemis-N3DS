#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CachedRemoteApp {
    int id = 0;
    std::string name;
};

// SD cache under /3ds/moonlight/applist/ — show instantly, refresh later.
bool load_cached_app_list(const std::string &address, std::uint16_t port,
                          std::vector<CachedRemoteApp> &apps);
bool save_cached_app_list(const std::string &address, std::uint16_t port,
                          const std::vector<CachedRemoteApp> &apps);
void sort_cached_app_list(std::vector<CachedRemoteApp> &apps);
bool app_name_matches_filter(const std::string &name, const std::string &filter);
