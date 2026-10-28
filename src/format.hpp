#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corekv::detail {

struct Entry {
  std::string key;
  std::optional<std::string> value;
};

enum class ReadStatus { record, eof, incomplete };

void write_record(std::FILE* file, const Entry& entry);
void validate_entry(const Entry& entry);
ReadStatus read_record(std::FILE* file, Entry& entry);
void validate_batch(const std::vector<Entry>& entries);
void write_batch_record(std::FILE* file, const std::vector<Entry>& entries);
ReadStatus read_wal_record(std::FILE* file, std::vector<Entry>& entries);
void sync_file(std::FILE* file);
std::uint64_t file_offset(std::FILE* file);
void seek_file(std::FILE* file, std::uint64_t offset);
std::FILE* open_file(const std::filesystem::path& path, const char* mode);

}  // namespace corekv::detail
