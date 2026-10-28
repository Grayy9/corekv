#include "format.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace corekv::detail {
namespace {
constexpr std::size_t max_key = 64 * 1024;
constexpr std::size_t max_value = 1024 * 1024;
constexpr std::size_t max_body = 9 + max_key + max_value;
constexpr std::size_t max_batch_body = 4 * 1024 * 1024;
constexpr std::size_t max_batch_entries = 1024;

void put32(std::vector<unsigned char>& bytes, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<unsigned char>(value >> shift));
}

std::uint32_t get32(const unsigned char* data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) |
         (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
}

std::uint32_t checksum(const unsigned char* data, std::size_t size) {
  std::uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
  }
  return ~crc;
}

void write_body(std::FILE* file, const std::vector<unsigned char>& body) {
  std::vector<unsigned char> header;
  put32(header, static_cast<std::uint32_t>(body.size()));
  put32(header, checksum(body.data(), body.size()));
  if (std::fwrite(header.data(), 1, header.size(), file) != header.size() ||
      std::fwrite(body.data(), 1, body.size(), file) != body.size())
    throw std::runtime_error("record write failed");
}

ReadStatus read_body(std::FILE* file, std::vector<unsigned char>& body,
                     std::size_t maximum_size) {
  std::array<unsigned char, 8> header{};
  const auto header_read = std::fread(header.data(), 1, header.size(), file);
  if (header_read == 0 && std::feof(file)) return ReadStatus::eof;
  if (header_read != header.size()) {
    if (std::ferror(file)) throw std::runtime_error("record header read failed");
    return ReadStatus::incomplete;
  }
  const auto size = get32(header.data());
  if (size < 10 || size > maximum_size) throw std::runtime_error("invalid record length");
  body.resize(size);
  const auto body_read = std::fread(body.data(), 1, size, file);
  if (body_read != size) {
    if (std::ferror(file)) throw std::runtime_error("record body read failed");
    return ReadStatus::incomplete;
  }
  if (checksum(body.data(), body.size()) != get32(header.data() + 4))
    throw std::runtime_error("record checksum mismatch");
  return ReadStatus::record;
}

Entry decode_entry(const std::vector<unsigned char>& body, std::size_t& offset) {
  if (body.size() - offset < 10) throw std::runtime_error("invalid record body");
  const auto* data = body.data() + offset;
  const auto key_size = get32(data + 1);
  const auto value_size = get32(data + 5);
  if ((data[0] != 1 && data[0] != 2) || key_size == 0 || key_size > max_key ||
      value_size > max_value || (data[0] == 2 && value_size != 0) ||
      static_cast<std::uint64_t>(9) + key_size + value_size > body.size() - offset)
    throw std::runtime_error("invalid record body");
  Entry entry;
  entry.key.assign(reinterpret_cast<const char*>(data + 9), key_size);
  if (data[0] == 1)
    entry.value = std::string(reinterpret_cast<const char*>(data + 9 + key_size), value_size);
  offset += 9 + key_size + value_size;
  return entry;
}
}  // namespace

void validate_entry(const Entry& entry) {
  if (entry.key.empty() || entry.key.size() > max_key ||
      (entry.value && entry.value->size() > max_value))
    throw std::invalid_argument("key or value size outside supported range");
}

void write_record(std::FILE* file, const Entry& entry) {
  validate_entry(entry);
  std::vector<unsigned char> body;
  body.reserve(9 + entry.key.size() + (entry.value ? entry.value->size() : 0));
  body.push_back(entry.value ? 1 : 2);
  put32(body, static_cast<std::uint32_t>(entry.key.size()));
  put32(body, static_cast<std::uint32_t>(entry.value ? entry.value->size() : 0));
  body.insert(body.end(), entry.key.begin(), entry.key.end());
  if (entry.value) body.insert(body.end(), entry.value->begin(), entry.value->end());
  write_body(file, body);
}

ReadStatus read_record(std::FILE* file, Entry& entry) {
  std::vector<unsigned char> body;
  const auto status = read_body(file, body, max_body);
  if (status != ReadStatus::record) return status;
  std::size_t offset = 0;
  entry = decode_entry(body, offset);
  if (offset != body.size()) throw std::runtime_error("invalid record body");
  return ReadStatus::record;
}

void validate_batch(const std::vector<Entry>& entries) {
  if (entries.empty() || entries.size() > max_batch_entries)
    throw std::invalid_argument("batch size outside supported range");
  std::size_t size = 5;
  for (const auto& entry : entries) {
    validate_entry(entry);
    size += 9 + entry.key.size() + (entry.value ? entry.value->size() : 0);
    if (size > max_batch_body) throw std::invalid_argument("batch record exceeds 4 MiB");
  }
}

void write_batch_record(std::FILE* file, const std::vector<Entry>& entries) {
  validate_batch(entries);
  std::vector<unsigned char> body;
  body.push_back(3);
  put32(body, static_cast<std::uint32_t>(entries.size()));
  for (const auto& entry : entries) {
    body.push_back(entry.value ? 1 : 2);
    put32(body, static_cast<std::uint32_t>(entry.key.size()));
    put32(body, static_cast<std::uint32_t>(entry.value ? entry.value->size() : 0));
    body.insert(body.end(), entry.key.begin(), entry.key.end());
    if (entry.value) body.insert(body.end(), entry.value->begin(), entry.value->end());
  }
  write_body(file, body);
}

ReadStatus read_wal_record(std::FILE* file, std::vector<Entry>& entries) {
  std::vector<unsigned char> body;
  const auto status = read_body(file, body, max_batch_body);
  if (status != ReadStatus::record) return status;
  std::vector<Entry> decoded;
  std::size_t offset = 0;
  if (body[0] == 3) {
    if (body.size() < 15) throw std::runtime_error("invalid batch record");
    const auto count = get32(body.data() + 1);
    if (count == 0 || count > max_batch_entries)
      throw std::runtime_error("invalid batch count");
    offset = 5;
    decoded.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) decoded.push_back(decode_entry(body, offset));
  } else {
    decoded.push_back(decode_entry(body, offset));
  }
  if (offset != body.size()) throw std::runtime_error("invalid WAL record body");
  entries = std::move(decoded);
  return ReadStatus::record;
}

void sync_file(std::FILE* file) {
  if (std::fflush(file) != 0) throw std::runtime_error("file flush failed");
#ifdef _WIN32
  if (_commit(_fileno(file)) != 0) throw std::runtime_error("file sync failed");
#else
  if (fsync(fileno(file)) != 0) throw std::runtime_error("file sync failed");
#endif
}

std::uint64_t file_offset(std::FILE* file) {
#ifdef _WIN32
  const auto offset = _ftelli64(file);
#else
  const auto offset = ftello(file);
#endif
  if (offset < 0) throw std::runtime_error("file offset failed");
  return static_cast<std::uint64_t>(offset);
}

void seek_file(std::FILE* file, std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    throw std::runtime_error("file offset too large");
#ifdef _WIN32
  if (_fseeki64(file, static_cast<std::int64_t>(offset), SEEK_SET) != 0)
#else
  if (fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0)
#endif
    throw std::runtime_error("file seek failed");
}

std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
  std::wstring wide_mode;
  for (const char* character = mode; *character; ++character)
    wide_mode.push_back(static_cast<wchar_t>(*character));
  return _wfopen(path.wstring().c_str(), wide_mode.c_str());
#else
  return std::fopen(path.string().c_str(), mode);
#endif
}

}  // namespace corekv::detail
