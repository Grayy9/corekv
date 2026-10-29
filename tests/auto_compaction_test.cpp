#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::size_t sst_count(const fs::path& path) {
  std::size_t count = 0;
  for (const auto& file : fs::directory_iterator(path))
    if (file.path().extension() == ".dat") ++count;
  return count;
}

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-auto-compact-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    {
      auto db = corekv::DB::Open(path, {.memtable_limit = 1, .compaction_trigger = 3});
      db->Put("a", "old");
      db->Put("b", "two");
      check(sst_count(path) == 2, "below threshold keeps SSTs");
      check(db->GetStats().sst_files == 2 && db->GetStats().compactions == 0,
            "stats before threshold");
      db->Delete("a");
      check(sst_count(path) == 1, "threshold compacts SSTs");
      check(db->GetStats().sst_files == 1 && db->GetStats().compactions == 1,
            "stats after automatic compaction");
      check(!db->Get("a") && db->Get("b") == "two", "latest states visible");
    }
    {
      auto db = corekv::DB::Open(path, {.memtable_limit = 1, .compaction_trigger = 3});
      check(!db->Get("a") && db->Get("b") == "two", "compaction survives restart");
      db->Put("a", "new");
      db->Put("c", "three");
      check(sst_count(path) == 1, "later writes trigger another compaction");
      check(db->GetStats().compactions == 1, "compaction count starts at open");
      check(db->Get("a") == "new", "new value survives tombstone");
    }
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("a") == "new" && db->Get("c") == "three", "second compaction survives restart");
    }
    const auto disabled = path / "disabled";
    {
      auto db = corekv::DB::Open(disabled, {.memtable_limit = 1, .compaction_trigger = 0});
      for (int i = 0; i < 4; ++i) db->Put("key-" + std::to_string(i), "v");
      check(sst_count(disabled) == 4, "zero disables automatic compaction");
      check(db->GetStats().sst_files == 4 && db->GetStats().compactions == 0,
            "disabled compaction stats");
      db->Compact();
      check(db->GetStats().sst_files == 1 && db->GetStats().compactions == 1,
            "manual compaction updates stats");
    }
    {
      auto db = corekv::DB::Open(path / "metrics");
      db->Put("pending", "value");
      check(db->GetStats().memtable_entries == 1 && db->GetStats().sst_files == 0,
            "pending write appears in stats");
      db->Flush();
      check(db->GetStats().memtable_entries == 0 && db->GetStats().sst_files == 1,
            "flush updates stats");
    }
    bool rejected = false;
    try { auto db = corekv::DB::Open(path / "invalid", {.compaction_trigger = 1}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "threshold of one is rejected");
  } catch (...) {
    fs::remove_all(path);
    throw;
  }
  fs::remove_all(path);
}
