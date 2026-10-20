#pragma once
#include "ingame_json.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace ep::ingame::filters {
struct Summary { std::string name, version; int rules = 0, enabled = 0; };
Summary Parse(const std::string& xml);
std::string Rename(const std::string& xml, const std::string& name);
json::Value List(const std::filesystem::path& root);
json::Value Duplicate(const std::filesystem::path& root, const std::string& file);
json::Value Export(const std::filesystem::path& root, const std::filesystem::path& output, const std::string& file);
json::Value Import(const std::filesystem::path& root, const std::filesystem::path& source);
json::Value Backup(const std::filesystem::path& root, const std::filesystem::path& output);
std::filesystem::path Root();
std::filesystem::path UserRoot();
} // namespace ep::ingame::filters
