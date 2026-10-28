#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-batch-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    {
      auto db = corekv::DB::Open(path, {.memtable_limit = 16});
      db->Put("old", "value");
      db->WriteBatch({{"a", "first"}, {"b", "second"}, {"a", "last"},
                      {"old", std::nullopt}});
      check(db->Get("a") == "last", "last mutation wins");
      check(db->Get("b") == "second", "second key visible");
      check(!db->Get("old"), "delete visible");
      check(fs::file_size(path / "WAL") == 0, "complete batch flushed together");
      db->WriteBatch({});
      bool invalid = false;
      try { db->WriteBatch({{"good", "yes"}, {"", "bad"}}); }
      catch (const std::invalid_argument&) { invalid = true; }
      check(invalid, "invalid batch rejected");
      check(!db->Get("good"), "invalid batch has no partial effect");
      std::vector<corekv::Mutation> too_many(1025, {"key", "value"});
      invalid = false;
      try { db->WriteBatch(too_many); }
      catch (const std::invalid_argument&) { invalid = true; }
      check(invalid, "oversized batch rejected");
      db->Put("after", "ok");
    }
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("a") == "last", "batch survives restart");
      check(db->Get("b") == "second", "second key survives restart");
      check(!db->Get("old"), "delete survives restart");
      check(db->Get("after") == "ok", "write after invalid batch survives");
      db->Compact();
    }
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("a") == "last", "batch survives compaction");
      check(!db->Get("old"), "batch tombstone survives compaction");
    }
  } catch (...) {
    fs::remove_all(path);
    throw;
  }
  fs::remove_all(path);
}
