#include "wal.hpp"

#include <stdexcept>

namespace corekv::detail {

Wal::Wal(std::filesystem::path path) : path_(std::move(path)) {}
Wal::~Wal() { if (file_) std::fclose(file_); }

void Wal::Replay(std::map<std::string, std::optional<std::string>>& entries) {
  if (file_) throw std::logic_error("WAL already open");
  if (std::filesystem::exists(path_)) {
    std::unique_ptr<std::FILE, decltype(&std::fclose)> input(
        open_file(path_, "rb"), &std::fclose);
    if (!input) throw std::runtime_error("cannot open WAL");
    std::uint64_t valid_end = 0;
    while (true) {
      std::vector<Entry> record;
      const auto status = read_wal_record(input.get(), record);
      if (status == ReadStatus::eof) break;
      if (status == ReadStatus::incomplete) {
        input.reset();
        std::filesystem::resize_file(path_, valid_end);
        break;
      }
      for (auto& entry : record)
        entries[std::move(entry.key)] = std::move(entry.value);
      valid_end = file_offset(input.get());
    }
  }
  file_ = open_file(path_, "ab");
  if (!file_) throw std::runtime_error("cannot open WAL for append");
}

void Wal::Append(const Entry& entry) {
  if (!file_) throw std::logic_error("WAL not open");
  write_record(file_, entry);
  sync_file(file_);
}

void Wal::AppendBatch(const std::vector<Entry>& entries) {
  if (!file_) throw std::logic_error("WAL not open");
  write_batch_record(file_, entries);
  sync_file(file_);
}

void Wal::Reset() {
  std::FILE* old = file_;
  file_ = nullptr;
  if (std::fclose(old) != 0) throw std::runtime_error("WAL close failed");
  file_ = open_file(path_, "wb");
  if (!file_) throw std::runtime_error("WAL reset failed");
  sync_file(file_);
}

}  // namespace corekv::detail
