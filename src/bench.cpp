#include "corekv/db.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: corekv_bench <fresh-directory> <operations> [batch-size]\n";
    return 2;
  }
  try {
    std::size_t consumed = 0;
    const std::string argument = argv[2];
    const auto count = std::stoull(argument, &consumed);
    if (consumed != argument.size() || count == 0 || count > 1000000)
      throw std::invalid_argument("invalid operation count");
    std::uint64_t batch_size = 1;
    if (argc == 4) {
      const std::string batch_argument = argv[3];
      consumed = 0;
      batch_size = std::stoull(batch_argument, &consumed);
      if (consumed != batch_argument.size() || batch_size == 0 || batch_size > 1024)
        throw std::invalid_argument("invalid batch size");
    }
    using Clock = std::chrono::steady_clock;
    auto db = corekv::DB::Open(argv[1], {.memtable_limit = 1024 * 1024});
    const std::string value(128, 'x');
    const auto write_start = Clock::now();
    for (std::uint64_t i = 0; i < count;) {
      if (batch_size == 1) {
        db->Put("key-" + std::to_string(i), value);
        ++i;
      } else {
        std::vector<corekv::Mutation> batch;
        const auto size = std::min(batch_size, count - i);
        batch.reserve(static_cast<std::size_t>(size));
        for (std::uint64_t j = 0; j < size; ++j)
          batch.push_back({"key-" + std::to_string(i + j), value});
        db->WriteBatch(batch);
        i += size;
      }
    }
    const auto write_end = Clock::now();
    db->Flush();
    db.reset();
    db = corekv::DB::Open(argv[1]);
    const auto read_start = Clock::now();
    for (std::uint64_t i = 0; i < count; ++i) {
      if (db->Get("key-" + std::to_string(i)) != value)
        throw std::runtime_error("read verification failed");
    }
    const auto read_end = Clock::now();
    const auto writes = std::chrono::duration<double>(write_end - write_start).count();
    const auto reads = std::chrono::duration<double>(read_end - read_start).count();
    std::cout << "{\"writes_completed\":" << count
              << ",\"reads_completed\":" << count
              << ",\"value_bytes\":128,\"batch_size\":" << batch_size
              << ",\"write_mode\":\""
              << (batch_size == 1 ? "fsync_each_write" : "fsync_each_batch") << "\""
              << ",\"read_mode\":\"sst_after_reopen\""
              << ",\"write_ops_per_second\":" << count / writes
              << ",\"read_ops_per_second\":" << count / reads << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
