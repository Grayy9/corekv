#include "corekv/db.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

int main() {
  const auto original = fs::current_path();
  const auto base = fs::temp_directory_path() /
      ("corekv-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    fs::create_directories(base / "first" / "data");
    fs::create_directories(base / "second" / "data");
    fs::current_path(base / "first");
    {
      auto db = corekv::DB::Open("data");
      db->Put("before", "one");
      fs::current_path(base / "second");
      db->Flush();
      db->Put("after", "two");
    }
    fs::current_path(original);
    auto db = corekv::DB::Open(base / "first" / "data");
    if (db->Get("before") != "one" || db->Get("after") != "two")
      throw std::runtime_error("directory changed after Open");
    db.reset();
  } catch (...) {
    fs::current_path(original);
    fs::remove_all(base);
    throw;
  }
  fs::remove_all(base);
}
