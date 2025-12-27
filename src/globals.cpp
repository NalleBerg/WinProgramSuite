// Central definitions for globals referenced across translation units
#include <vector>
#include <string>
#include <mutex>

// Package list: pair of <id, display name>
std::vector<std::pair<std::string,std::string>> g_packages;
std::mutex g_packages_mutex;
