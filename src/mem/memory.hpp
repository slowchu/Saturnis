#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace saturnis::mem {

struct StoreEntry {
  std::uint64_t store_id = 0;
  std::uint32_t phys = 0;
  std::uint8_t size = 4;
  std::uint32_t value = 0;
};

class StoreBuffer {
public:
  void push(StoreEntry entry);
  [[nodiscard]] std::optional<std::uint32_t> forward(std::uint32_t phys, std::uint8_t size) const;
  [[nodiscard]] bool retire(std::uint64_t store_id);
  [[nodiscard]] std::size_t size() const;

private:
  std::deque<StoreEntry> entries_;
};

struct CacheLine {
  bool valid = false;
  std::uint32_t tag = 0;
  std::vector<std::uint8_t> bytes;
};

class TinyCache {
public:
  TinyCache(std::size_t line_size, std::size_t line_count);
  [[nodiscard]] std::size_t line_size() const;
  [[nodiscard]] bool read(std::uint32_t phys, std::uint8_t size, std::uint32_t &out) const;
  void write(std::uint32_t phys, std::uint8_t size, std::uint32_t value);
  void fill_line(std::uint32_t line_base, const std::vector<std::uint8_t> &line_data);

private:
  std::size_t line_size_;
  std::vector<CacheLine> lines_;
};

class CommittedMemory {
public:
  explicit CommittedMemory(std::size_t size_bytes = 32U * 1024U * 1024U);
  [[nodiscard]] std::uint32_t read(std::uint32_t phys, std::uint8_t size) const;
  void write(std::uint32_t phys, std::uint8_t size, std::uint32_t value);
  [[nodiscard]] std::vector<std::uint8_t> read_block(std::uint32_t phys, std::size_t size) const;

private:
  std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] std::uint32_t to_phys(std::uint32_t vaddr);
[[nodiscard]] bool is_uncached_alias(std::uint32_t vaddr);
[[nodiscard]] bool is_mmio(std::uint32_t phys);

} // namespace saturnis::mem
