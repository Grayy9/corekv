#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  const auto path = fs::temp_directory_path() /
      ("corekv-batch-recovery-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    {
      auto db = corekv::DB::Open(path);
      db->Put("stable", "before");
    }
    const auto valid_end = fs::file_size(path / "WAL");
    {
      auto db = corekv::DB::Open(path);
      db->WriteBatch({{"first", "one"}, {"stable", std::nullopt}, {"second", "two"}});
    }
    fs::resize_file(path / "WAL", valid_end + 10);
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("stable") == "before", "partial batch must not delete earlier value");
      check(!db->Get("first") && !db->Get("second"),
            "partial batch must not apply any mutation");
      check(fs::file_size(path / "WAL") == valid_end, "partial batch removed");
      db->WriteBatch({{"first", "one"}, {"second", "two"}});
    }
    {
      auto db = corekv::DB::Open(path);
      check(db->Get("first") == "one" && db->Get("second") == "two",
            "complete batch survives restart");
    }
  } catch (...) {
    fs::remove_all(path);
    throw;
  }
  fs::remove_all(path);
}
