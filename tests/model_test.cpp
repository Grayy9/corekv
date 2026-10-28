#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-model-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::map<std::string, std::optional<std::string>> expected;
  std::mt19937 generator(20261028);
  try {
    auto db = corekv::DB::Open(path, {.memtable_limit = 96});
    for (int step = 0; step < 400; ++step) {
      const auto key = "key-" + std::to_string(generator() % 31);
      if (step % 9 == 0) {
        const auto second = "key-" + std::to_string(generator() % 31);
        const auto value = "batch-" + std::to_string(step);
        db->WriteBatch({{key, value}, {second, std::nullopt}, {key, value + "-last"}});
        expected[key] = value;
        expected[second] = std::nullopt;
        expected[key] = value + "-last";
      } else if (generator() % 5 == 0) {
        db->Delete(key);
        expected[key] = std::nullopt;
      } else {
        const auto value = "value-" + std::to_string(step);
        db->Put(key, value);
        expected[key] = value;
      }
      if (step % 37 == 0) db->Compact();
      if (step % 53 == 0) {
        db.reset();
        db = corekv::DB::Open(path, {.memtable_limit = 96});
      }
      for (int id = 0; id < 31; ++id) {
        const auto probe = "key-" + std::to_string(id);
        const auto found = expected.find(probe);
        const auto want = found == expected.end() ? std::nullopt : found->second;
        if (db->Get(probe) != want) throw std::runtime_error("model mismatch");
      }
      std::vector<std::pair<std::string, std::string>> visible;
      for (const auto& [name, value] : expected)
        if (value) visible.emplace_back(name, *value);
      if (db->Scan("", "", 31) != visible) throw std::runtime_error("scan model mismatch");
    }
  } catch (...) {
    fs::remove_all(path);
    throw;
  }
  fs::remove_all(path);
}
