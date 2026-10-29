#include "corekv/db.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint64_t parse_count(const char* text, std::uint64_t maximum) {
  const std::string argument(text);
  std::size_t consumed = 0;
  const auto value = std::stoull(argument, &consumed);
  if (consumed != argument.size() || value > maximum)
    throw std::invalid_argument("invalid benchmark argument");
  return value;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "usage: corekv_compaction_bench <fresh-directory> <operations> <memtable-bytes> <compaction-trigger>\n";
    return 2;
  }
  try {
    const auto count = parse_count(argv[2], 100000);
    const auto memtable_bytes = parse_count(argv[3], 1024 * 1024 * 1024);
    const auto trigger = parse_count(argv[4], 1000);
    if (count == 0 || memtable_bytes == 0 || trigger == 1)
      throw std::invalid_argument("operation count and memtable size must be positive; trigger must be zero or at least two");
    using Clock = std::chrono::steady_clock;
    const std::string value(128, 'x');
    auto db = corekv::DB::Open(argv[1], {.memtable_limit = static_cast<std::size_t>(memtable_bytes),
                                         .compaction_trigger = static_cast<std::size_t>(trigger)});
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
      const auto start = Clock::now();
      db->Put("key-" + std::to_string(i), value);
      latencies.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    const auto stats = db->GetStats();
    db.reset();
    db = corekv::DB::Open(argv[1]);
    for (std::uint64_t i = 0; i < count; ++i)
      if (db->Get("key-" + std::to_string(i)) != value)
        throw std::runtime_error("read verification failed");
    std::sort(latencies.begin(), latencies.end());
    const auto percentile = [&](std::uint64_t numerator) {
      return latencies[static_cast<std::size_t>((count * numerator + 99) / 100 - 1)];
    };
    std::cout << "{\"writes_completed\":" << count
              << ",\"memtable_bytes\":" << memtable_bytes
              << ",\"compaction_trigger\":" << trigger
              << ",\"compactions\":" << stats.compactions
              << ",\"sst_files\":" << stats.sst_files
              << ",\"write_p50_ms\":" << percentile(50)
              << ",\"write_p99_ms\":" << percentile(99)
              << ",\"write_max_ms\":" << latencies.back() << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
