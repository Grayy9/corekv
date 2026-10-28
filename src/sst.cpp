#include "sst.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace corekv::detail {
namespace {
constexpr std::array<char, 4> magic{'S', 'S', 'T', '1'};
using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

File read_sst(const std::filesystem::path& path) {
  File file(open_file(path, "rb"), &std::fclose);
  if (!file) throw std::runtime_error("cannot open SST");
  std::array<char, 4> actual{};
  if (std::fread(actual.data(), 1, actual.size(), file.get()) != actual.size() ||
      actual != magic) throw std::runtime_error("invalid SST header");
  return file;
}
}  // namespace

SstFile SstFile::Load(std::filesystem::path path, std::uint64_t id) {
  SstFile result;
  result.path_ = std::move(path);
  result.id_ = id;
  auto file = read_sst(result.path_);
  std::string previous;
  while (true) {
    const auto offset = file_offset(file.get());
    Entry entry;
    const auto status = read_record(file.get(), entry);
    if (status == ReadStatus::eof) break;
    if (status == ReadStatus::incomplete) throw std::runtime_error("truncated SST");
    if (!result.index_.empty() && entry.key <= previous)
      throw std::runtime_error("SST keys are not strictly ordered");
    previous = entry.key;
    result.index_.emplace_back(std::move(entry.key), offset);
  }
  result.file_.reset(file.release(), &std::fclose);
  return result;
}

SstFile SstFile::Write(std::filesystem::path path, std::uint64_t id,
                       const std::map<std::string, std::optional<std::string>>& entries) {
  auto temporary = path;
  temporary += ".tmp";
  File file(open_file(temporary, "wb"), &std::fclose);
  if (!file) throw std::runtime_error("cannot create SST");
  if (std::fwrite(magic.data(), 1, magic.size(), file.get()) != magic.size())
    throw std::runtime_error("SST header write failed");
  for (const auto& [key, value] : entries) write_record(file.get(), {key, value});
  sync_file(file.get());
  if (std::fclose(file.release()) != 0) throw std::runtime_error("SST close failed");
  std::filesystem::rename(temporary, path);
  return Load(std::move(path), id);
}

std::optional<Entry> SstFile::Find(const std::string& key) const {
  const auto position = LowerBound(key);
  if (position == Size() || KeyAt(position) != key) return std::nullopt;
  return EntryAt(position);
}

std::size_t SstFile::LowerBound(const std::string& key) const {
  const auto it = std::lower_bound(index_.begin(), index_.end(), key,
                                   [](const auto& item, const std::string& needle) {
                                     return item.first < needle;
                                   });
  return static_cast<std::size_t>(it - index_.begin());
}

Entry SstFile::EntryAt(std::size_t position) const {
  const auto& [key, offset] = index_.at(position);
  seek_file(file_.get(), offset);
  Entry entry;
  if (read_record(file_.get(), entry) != ReadStatus::record || entry.key != key)
    throw std::runtime_error("SST index mismatch");
  return entry;
}

std::vector<Entry> SstFile::All() const {
  std::vector<Entry> result;
  result.reserve(index_.size());
  for (const auto& [key, offset] : index_) {
    seek_file(file_.get(), offset);
    Entry entry;
    if (read_record(file_.get(), entry) != ReadStatus::record || entry.key != key)
      throw std::runtime_error("SST index mismatch");
    result.push_back(std::move(entry));
  }
  return result;
}

}  // namespace corekv::detail
