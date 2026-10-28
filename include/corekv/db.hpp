#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace corekv {

struct Options {
  std::size_t memtable_limit = 1024 * 1024;
};

struct Mutation {
  std::string key;
  std::optional<std::string> value;  // nullopt means Delete
};

class DB {
 public:
  static std::unique_ptr<DB> Open(const std::filesystem::path& directory, Options options = {});
  ~DB();
  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  void Put(const std::string& key, const std::string& value);
  std::optional<std::string> Get(const std::string& key);
  std::vector<std::pair<std::string, std::string>> Scan(
      const std::string& begin, const std::string& end, std::size_t limit);
  void Delete(const std::string& key);
  void WriteBatch(const std::vector<Mutation>& mutations);
  void Flush();
  void Compact();

 private:
  struct Impl;
  explicit DB(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace corekv
