#include "BWPack.h"

#include <cstdio>
#include <cstring>

namespace bw {

namespace {
constexpr char kMagic[8] = {'L', 'i', 'O', 'n', 'H', 'e', 'A', 'd'};
constexpr std::size_t kNameSize = 32;
constexpr std::size_t kBlockHeader = kNameSize + 4;
}  // namespace

bool Pack::load(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  if (got != bytes.size()) return false;
  return parse(std::move(bytes));
}

bool Pack::parse(std::vector<std::uint8_t> bytes) {
  bytes_.clear();
  blocks_.clear();
  if (bytes.size() < sizeof(kMagic) + kBlockHeader) return false;
  if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;

  std::vector<PackBlock> parsed;
  std::size_t off = sizeof(kMagic);
  while (off < bytes.size()) {
    if (off + kBlockHeader > bytes.size()) return false;  // truncated header
    const char* raw = reinterpret_cast<const char*>(bytes.data() + off);
    std::size_t len = 0;
    while (len < kNameSize && raw[len] != '\0') ++len;
    std::uint32_t size = static_cast<std::uint32_t>(bytes[off + kNameSize]) |
                         static_cast<std::uint32_t>(bytes[off + kNameSize + 1]) << 8 |
                         static_cast<std::uint32_t>(bytes[off + kNameSize + 2]) << 16 |
                         static_cast<std::uint32_t>(bytes[off + kNameSize + 3]) << 24;
    off += kBlockHeader;
    if (size > bytes.size() - off) return false;  // body overruns the file
    parsed.push_back({std::string(raw, len), off, size});
    off += size;
  }
  if (parsed.empty()) return false;

  bytes_ = std::move(bytes);
  blocks_ = std::move(parsed);
  return true;
}

const std::uint8_t* Pack::block(const std::string& name, std::uint32_t* size) const {
  for (const PackBlock& b : blocks_) {
    if (b.name == name) {
      if (size) *size = b.size;
      return bytes_.data() + b.offset;
    }
  }
  if (size) *size = 0;
  return nullptr;
}

}  // namespace bw
