#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-db-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    {
      auto db = corekv::DB::Open(path, {.memtable_limit = 60});
      db->Put("alpha", "one");
      db->Put("beta", "two");
      check(db->Get("alpha") == "one", "put/get");
      db->Put("alpha", "new");
      db->Delete("beta");
      check(!db->Get("beta"), "delete");
      db->Flush();
      db->Put("gamma", "three");
      check(db->Get("alpha") == "new", "read sst");
    }
    {
      auto db = corekv::DB::Open(path, {.memtable_limit = 60});
      check(db->Get("alpha") == "new", "restart sst");
      check(!db->Get("beta"), "restart tombstone");
      check(db->Get("gamma") == "three", "restart wal");
      db->Flush();
      db->Put("alpha", "last");
      db->Flush();
      fs::path old_sst;
      for (const auto& file : fs::directory_iterator(path)) {
        if (file.path().extension() == ".dat") {
          old_sst = file.path();
          break;
        }
      }
      check(!old_sst.empty(), "old SST exists before compaction");
      auto backup = old_sst;
      backup += ".bak";
      fs::copy_file(old_sst, backup);
      db->Compact();
      fs::copy_file(backup, old_sst);
    }
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("alpha") == "last", "compacted latest value");
      check(!db->Get("beta"), "compacted tombstone");
      check(db->Get("gamma") == "three", "compacted other value");
      bool rejected = false;
      try { db->Put("", "invalid"); } catch (const std::invalid_argument&) { rejected = true; }
      check(rejected, "empty key rejected");
    }
    const auto auto_dir = path / "automatic-flush";
    {
      auto db = corekv::DB::Open(auto_dir, {.memtable_limit = 8});
      db->Put("a", "123456");
      check(db->Get("a") == "123456", "automatic flush read");
      check(fs::file_size(auto_dir / "WAL") == 0, "automatic flush reset WAL");
      std::size_t sst_count = 0;
      for (const auto& file : fs::directory_iterator(auto_dir))
        if (file.path().extension() == ".dat") ++sst_count;
      check(sst_count == 1, "automatic flush wrote SST");
    }
    {
      auto db = corekv::DB::Open(auto_dir);
      check(db->Get("a") == "123456", "automatic flush survives restart");
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    fs::remove_all(path);
    return 1;
  }
  fs::remove_all(path);
}
