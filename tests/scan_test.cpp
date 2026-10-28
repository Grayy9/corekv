#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Rows = std::vector<std::pair<std::string, std::string>>;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void verify(corekv::DB& db) {
  check(db.Scan("a", "f", 10) == Rows({{"a", "new"}, {"c", "middle"}, {"e", "five"}}),
        "merge latest values and tombstones");
  check(db.Scan("a", "f", 2) == Rows({{"a", "new"}, {"c", "middle"}}),
        "limit visible rows");
  check(db.Scan("b", "d", 10) == Rows({{"c", "middle"}}), "half-open bounds");
  check(db.Scan("x", "", 10) == Rows({{"z", "last"}}), "open upper bound");
  check(db.Scan("f", "a", 10).empty(), "reversed range");
  check(db.Scan("a", "a", 10).empty(), "empty range");
  check(db.Scan("a", "f", 0).empty(), "zero limit");
  check(db.Scan("", "a", 10) == Rows({{std::string("\0a", 2), "binary"}}),
        "binary key before text range");
}

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-scan-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    {
      auto db = corekv::DB::Open(path);
      db->Put(std::string("\0a", 2), "binary");
      db->Put("a", "old");
      db->Put("b", "old");
      db->Put("d", "old");
      db->Put("z", "last");
      db->Flush();
      db->Put("a", "new");
      db->Delete("b");
      db->Put("c", "middle");
      db->Flush();
      db->Delete("d");
      db->Put("e", "five");
      verify(*db);
    }
    {
      auto db = corekv::DB::Open(path);
      verify(*db);
      db->Compact();
      verify(*db);
    }
    {
      auto db = corekv::DB::Open(path);
      verify(*db);
    }
  } catch (...) {
    fs::remove_all(path);
    throw;
  }
  fs::remove_all(path);
}
