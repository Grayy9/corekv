#include "corekv/db.hpp"

#include "sst.hpp"
#include "wal.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace corekv {
namespace {
class DirectoryLock {
 public:
  explicit DirectoryLock(const std::filesystem::path& path) {
#ifdef _WIN32
    handle_ = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("database is locked");
#else
    fd_ = open(path.string().c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ < 0) throw std::runtime_error("cannot open database lock");
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
      close(fd_);
      throw std::runtime_error("database is locked");
    }
#endif
  }
  ~DirectoryLock() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (fd_ >= 0) close(fd_);
#endif
  }
  DirectoryLock(const DirectoryLock&) = delete;
  DirectoryLock& operator=(const DirectoryLock&) = delete;

 private:
#ifdef _WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

std::string sst_name(std::uint64_t id) {
  std::ostringstream name;
  name << "sst-" << std::setw(20) << std::setfill('0') << id << ".dat";
  return name.str();
}

std::optional<std::uint64_t> parse_sst_name(const std::string& name) {
  if (name.size() != 28 || name.substr(0, 4) != "sst-" ||
      name.substr(24) != ".dat") return std::nullopt;
  for (std::size_t i = 4; i < 24; ++i)
    if (name[i] < '0' || name[i] > '9') return std::nullopt;
  return std::stoull(name.substr(4, 20));
}

std::size_t memtable_size(const std::map<std::string, std::optional<std::string>>& entries) {
  std::size_t size = 0;
  for (const auto& [key, value] : entries) size += key.size() + (value ? value->size() : 0) + 1;
  return size;
}
}  // namespace

struct DB::Impl {
  std::filesystem::path directory;
  Options options;
  DirectoryLock lock;
  std::mutex mutex;
  std::map<std::string, std::optional<std::string>> memtable;
  std::vector<detail::SstFile> ssts;
  std::unique_ptr<detail::Wal> wal;
  std::uint64_t next_id = 1;
  std::size_t compactions = 0;
  bool write_failed = false;

  Impl(std::filesystem::path path, Options config)
      : directory(std::move(path)), options(config), lock(directory / "LOCK") {
    if (options.memtable_limit == 0) throw std::invalid_argument("memtable limit must be positive");
    if (options.compaction_trigger == 1)
      throw std::invalid_argument("compaction trigger must be zero or at least two");
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
      if (!file.is_regular_file()) continue;
      const auto id = parse_sst_name(file.path().filename().string());
      if (id) ssts.push_back(detail::SstFile::Load(file.path(), *id));
    }
    std::sort(ssts.begin(), ssts.end(), [](const auto& a, const auto& b) {
      return a.id() < b.id();
    });
    if (!ssts.empty()) next_id = ssts.back().id() + 1;
    wal = std::make_unique<detail::Wal>(directory / "WAL");
    wal->Replay(memtable);
  }

  void compact_files() {
    if (ssts.size() <= 1) return;
    std::map<std::string, std::optional<std::string>> merged;
    for (const auto& sst : ssts)
      for (auto& entry : sst.All()) merged[std::move(entry.key)] = std::move(entry.value);
    auto compacted = detail::SstFile::Write(directory / sst_name(next_id), next_id, merged);
    ++next_id;
    std::vector<std::filesystem::path> old_paths;
    old_paths.reserve(ssts.size());
    for (const auto& old : ssts) old_paths.push_back(old.path());
    ssts.clear();
    for (const auto& old : old_paths) std::filesystem::remove(old);
    ssts.push_back(std::move(compacted));
    ++compactions;
  }

  void flush() {
    if (memtable.empty()) return;
    auto file = detail::SstFile::Write(directory / sst_name(next_id), next_id, memtable);
    ++next_id;
    ssts.push_back(std::move(file));
    memtable.clear();
    wal->Reset();
    if (options.compaction_trigger != 0 && ssts.size() >= options.compaction_trigger)
      compact_files();
  }

  void write(const std::string& key, std::optional<std::string> value) {
    detail::validate_entry({key, value});
    if (write_failed) throw std::runtime_error("database write path failed; reopen required");
    try {
      wal->Append({key, value});
      memtable[key] = std::move(value);
      if (memtable_size(memtable) >= options.memtable_limit) flush();
    } catch (...) {
      write_failed = true;
      throw;
    }
  }

  void write_batch(const std::vector<detail::Entry>& entries) {
    detail::validate_batch(entries);
    if (write_failed) throw std::runtime_error("database write path failed; reopen required");
    try {
      wal->AppendBatch(entries);
      for (const auto& entry : entries) memtable[entry.key] = entry.value;
      if (memtable_size(memtable) >= options.memtable_limit) flush();
    } catch (...) {
      write_failed = true;
      throw;
    }
  }
};

DB::DB(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
DB::~DB() = default;

std::unique_ptr<DB> DB::Open(const std::filesystem::path& directory, Options options) {
  const auto absolute = std::filesystem::absolute(directory);
  std::filesystem::create_directories(absolute);
  return std::unique_ptr<DB>(new DB(std::make_unique<Impl>(
      std::filesystem::canonical(absolute), options)));
}

void DB::Put(const std::string& key, const std::string& value) {
  if (key.empty()) throw std::invalid_argument("empty key");
  std::lock_guard guard(impl_->mutex);
  impl_->write(key, value);
}

std::optional<std::string> DB::Get(const std::string& key) {
  std::lock_guard guard(impl_->mutex);
  if (impl_->write_failed) throw std::runtime_error("database write path failed; reopen required");
  const auto memory = impl_->memtable.find(key);
  if (memory != impl_->memtable.end()) return memory->second;
  for (auto it = impl_->ssts.rbegin(); it != impl_->ssts.rend(); ++it) {
    if (auto entry = it->Find(key)) return entry->value;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> DB::Scan(
    const std::string& begin, const std::string& end, std::size_t limit) {
  std::vector<std::pair<std::string, std::string>> rows;
  std::lock_guard guard(impl_->mutex);
  if (impl_->write_failed) throw std::runtime_error("database write path failed; reopen required");
  if (limit == 0 || (!end.empty() && begin >= end)) return rows;
  auto memory = impl_->memtable.lower_bound(begin);
  std::vector<std::size_t> positions;
  positions.reserve(impl_->ssts.size());
  for (const auto& sst : impl_->ssts) positions.push_back(sst.LowerBound(begin));
  while (rows.size() < limit) {
    std::string key;
    bool found = false;
    if (memory != impl_->memtable.end()) {
      key = memory->first;
      found = true;
    }
    for (std::size_t i = 0; i < impl_->ssts.size(); ++i) {
      const auto& sst = impl_->ssts[i];
      if (positions[i] < sst.Size() && (!found || sst.KeyAt(positions[i]) < key)) {
        key = sst.KeyAt(positions[i]);
        found = true;
      }
    }
    if (!found || (!end.empty() && key >= end)) break;

    std::size_t latest_sst = impl_->ssts.size();
    std::size_t latest_position = 0;
    for (std::size_t i = 0; i < impl_->ssts.size(); ++i) {
      const auto& sst = impl_->ssts[i];
      if (positions[i] < sst.Size() && sst.KeyAt(positions[i]) == key) {
        latest_sst = i;
        latest_position = positions[i]++;
      }
    }
    std::optional<std::string> value;
    if (memory != impl_->memtable.end() && memory->first == key) {
      value = memory->second;
      ++memory;
    } else if (latest_sst < impl_->ssts.size()) {
      value = impl_->ssts[latest_sst].EntryAt(latest_position).value;
    }
    if (value) rows.emplace_back(std::move(key), std::move(*value));
  }
  return rows;
}

void DB::Delete(const std::string& key) {
  if (key.empty()) throw std::invalid_argument("empty key");
  std::lock_guard guard(impl_->mutex);
  impl_->write(key, std::nullopt);
}

void DB::WriteBatch(const std::vector<Mutation>& mutations) {
  if (mutations.empty()) return;
  std::vector<detail::Entry> entries;
  entries.reserve(mutations.size());
  for (const auto& mutation : mutations)
    entries.push_back({mutation.key, mutation.value});
  std::lock_guard guard(impl_->mutex);
  impl_->write_batch(entries);
}

void DB::Flush() {
  std::lock_guard guard(impl_->mutex);
  if (impl_->write_failed) throw std::runtime_error("database write path failed; reopen required");
  try { impl_->flush(); }
  catch (...) { impl_->write_failed = true; throw; }
}

void DB::Compact() {
  std::lock_guard guard(impl_->mutex);
  if (impl_->write_failed) throw std::runtime_error("database write path failed; reopen required");
  try {
    impl_->flush();
    impl_->compact_files();
  } catch (...) {
    impl_->write_failed = true;
    throw;
  }
}

Stats DB::GetStats() {
  std::lock_guard guard(impl_->mutex);
  return {impl_->ssts.size(), impl_->memtable.size(), impl_->compactions};
}

}  // namespace corekv
