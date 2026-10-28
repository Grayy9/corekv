#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void corrupt_last_byte(const fs::path& file) {
  const auto size = fs::file_size(file);
  std::fstream stream(file, std::ios::in | std::ios::out | std::ios::binary);
  stream.seekp(static_cast<std::streamoff>(size - 1));
  stream.put('!');
}

int main() {
  const auto base = fs::temp_directory_path() /
      ("corekv-recovery-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    const auto wal_dir = base / "wal";
    {
      auto db = corekv::DB::Open(wal_dir);
      db->Put(std::string("a\0b", 3), std::string("x\0y", 3));
      bool locked = false;
      try { auto second = corekv::DB::Open(wal_dir); }
      catch (const std::runtime_error&) { locked = true; }
      check(locked, "second writer must be refused");
      bool invalid = false;
      try { db->Put("oversized", std::string(1024 * 1024 + 1, 'x')); }
      catch (const std::invalid_argument&) { invalid = true; }
      check(invalid, "oversized value rejected");
      db->Put("after-invalid", "ok");
    }
    {
      std::ofstream tail(wal_dir / "WAL", std::ios::app | std::ios::binary);
      tail.write("\0\0\0", 3);
    }
    const auto valid_size = fs::file_size(wal_dir / "WAL") - 3;
    {
      auto db = corekv::DB::Open(wal_dir);
      check(db->Get(std::string("a\0b", 3)) == std::string("x\0y", 3), "binary recovery");
      check(db->Get("after-invalid") == "ok", "write after invalid input");
      check(fs::file_size(wal_dir / "WAL") == valid_size, "truncated WAL tail removed");
    }
    corrupt_last_byte(wal_dir / "WAL");
    bool bad_wal = false;
    try { auto db = corekv::DB::Open(wal_dir); }
    catch (const std::runtime_error&) { bad_wal = true; }
    check(bad_wal, "corrupt complete WAL must fail");

    const auto sst_dir = base / "sst";
    {
      auto db = corekv::DB::Open(sst_dir);
      db->Put("key", "value");
      db->Flush();
    }
    fs::path sst;
    for (const auto& file : fs::directory_iterator(sst_dir))
      if (file.path().extension() == ".dat") sst = file.path();
    check(!sst.empty(), "SST created");
    corrupt_last_byte(sst);
    bool bad_sst = false;
    try { auto db = corekv::DB::Open(sst_dir); }
    catch (const std::runtime_error&) { bad_sst = true; }
    check(bad_sst, "corrupt SST must fail");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    fs::remove_all(base);
    return 1;
  }
  fs::remove_all(base);
}
