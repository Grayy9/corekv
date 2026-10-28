#pragma once

#include "format.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace corekv::detail {

class Wal {
 public:
  explicit Wal(std::filesystem::path path);
  ~Wal();
  Wal(const Wal&) = delete;
  Wal& operator=(const Wal&) = delete;

  void Replay(std::map<std::string, std::optional<std::string>>& entries);
  void Append(const Entry& entry);
  void AppendBatch(const std::vector<Entry>& entries);
  void Reset();

 private:
  std::filesystem::path path_;
  std::FILE* file_ = nullptr;
};

}  // namespace corekv::detail
