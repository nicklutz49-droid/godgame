#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reader for Lionhead "LiOnHeAd" pack containers (AllMeshes.g3d and friends):
// an 8-byte magic, then blocks of {char[32] NUL-padded name, u32 bodySize} +
// body, concatenated to end of file. Format knowledge per docs/plan-assets.md
// §7 (openblack community documentation); implementation is our own.
//
// Personal-use runtime overlay: packs are read from the owner's installation,
// never shipped. GL-free. A failed parse leaves the pack empty (loaded() ==
// false) and the game on placeholders.
namespace bw {

struct PackBlock {
  std::string name;
  std::size_t offset = 0;  // body start within bytes()
  std::uint32_t size = 0;  // body size
};

class Pack {
 public:
  bool load(const std::string& path);
  bool parse(std::vector<std::uint8_t> bytes);  // takes ownership; tests feed this

  bool loaded() const { return !blocks_.empty(); }
  // Body pointer for the first block with this name, or nullptr.
  const std::uint8_t* block(const std::string& name, std::uint32_t* size) const;
  const std::vector<PackBlock>& blocks() const { return blocks_; }

 private:
  std::vector<std::uint8_t> bytes_;
  std::vector<PackBlock> blocks_;
};

}  // namespace bw
