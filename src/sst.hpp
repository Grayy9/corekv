#pragma once

#include "format.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace corekv::detail {

class SstFile {
 public:
  static SstFile Load(std::filesystem::path path, std::uint64_t id);
  static SstFile Write(std::filesystem::path path, std::uint64_t id,
                       const std::map<std::string, std::optional<std::string>>& entries);
  std::optional<Entry> Find(const std::string& key) const;
  std::size_t LowerBound(const std::string& key) const;
  std::size_t Size() const { return index_.size(); }
  const std::string& KeyAt(std::size_t position) const { return index_.at(position).first; }
  Entry EntryAt(std::size_t position) const;
  std::vector<Entry> All() const;
  const std::filesystem::path& path() const { return path_; }
  std::uint64_t id() const { return id_; }

 private:
  std::filesystem::path path_;
  std::uint64_t id_ = 0;
  std::vector<std::pair<std::string, std::uint64_t>> index_;
  std::shared_ptr<std::FILE> file_;
};

}  // namespace corekv::detail
