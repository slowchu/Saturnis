#include "mem/memory.hpp"

#include <algorithm>
#include <cassert>

namespace saturnis::mem {

void StoreBuffer::push(StoreEntry entry) {
  entries_.push_back(entry);
  if (entries_.size() > kMaxEntries) {
    entries_.pop_front();
  }
}

std::optional<std::uint32_t> StoreBuffer::forward(std::uint32_t phys, std::uint8_t size) const {
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (it->phys == phys && it->size == size) {
      return it->value;
    }
  }
  return std::nullopt;
}

TinyCache::TinyCache(std::size_t line_size, std::size_t line_count)
    : line_size_(line_size), lines_(line_count) {
  for (auto &line : lines_) {
    line.bytes.assign(line_size_, 0U);
  }
}

std::size_t TinyCache::line_size() const { return line_size_; }

bool TinyCache::read(std::uint32_t phys, std::uint8_t size, std::uint32_t &out) const {
  const std::uint32_t line_base = phys / static_cast<std::uint32_t>(line_size_);
  const std::size_t index = static_cast<std::size_t>(line_base % lines_.size());
  const auto &line = lines_[index];
  if (!line.valid || line.tag != line_base) {
    return false;
  }
  const std::size_t offset = static_cast<std::size_t>(phys % static_cast<std::uint32_t>(line_size_));
  if (offset + size > line_size_) {
    return false;
  }
  out = 0;
  for (std::size_t i = 0; i < size; ++i) {
    out |= static_cast<std::uint32_t>(line.bytes[offset + i]) << (8U * i);
  }
  return true;
}

void TinyCache::write(std::uint32_t phys, std::uint8_t size, std::uint32_t value) {
  const std::uint32_t line_base = phys / static_cast<std::uint32_t>(line_size_);
  const std::size_t index = static_cast<std::size_t>(line_base % lines_.size());
  auto &line = lines_[index];
  if (!line.valid || line.tag != line_base) {
    return;
  }
  const std::size_t offset = static_cast<std::size_t>(phys % static_cast<std::uint32_t>(line_size_));
  if (offset + size > line_size_) {
    return;
  }
  for (std::size_t i = 0; i < size; ++i) {
    line.bytes[offset + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
  }
}

void TinyCache::fill_line(std::uint32_t line_base, const std::vector<std::uint8_t> &line_data) {
  assert(line_data.size() == line_size_);
  const std::size_t index = static_cast<std::size_t>(line_base % lines_.size());
  auto &line = lines_[index];
  line.valid = true;
  line.tag = line_base;
  line.bytes = line_data;
}

CommittedMemory::CommittedMemory(std::size_t size_bytes) : bytes_(size_bytes, 0U) {}

std::uint32_t CommittedMemory::read(std::uint32_t phys, std::uint8_t size) const {
  std::uint32_t out = 0;
  for (std::size_t i = 0; i < size; ++i) {
    out |= static_cast<std::uint32_t>(bytes_[(phys + static_cast<std::uint32_t>(i)) % bytes_.size()]) << (8U * i);
  }
  return out;
}

void CommittedMemory::write(std::uint32_t phys, std::uint8_t size, std::uint32_t value) {
  for (std::size_t i = 0; i < size; ++i) {
    bytes_[(phys + static_cast<std::uint32_t>(i)) % bytes_.size()] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
  }
}

std::vector<std::uint8_t> CommittedMemory::read_block(std::uint32_t phys, std::size_t size) const {
  std::vector<std::uint8_t> out(size, 0U);
  for (std::size_t i = 0; i < size; ++i) {
    out[i] = bytes_[(phys + static_cast<std::uint32_t>(i)) % bytes_.size()];
  }
  return out;
}

std::uint32_t to_phys(std::uint32_t vaddr) { return vaddr & 0x1FFFFFFFU; }

bool is_uncached_alias(std::uint32_t vaddr) { return (vaddr & 0x20000000U) != 0U; }

bool is_mmio(std::uint32_t phys) {
  return (phys >= 0x05C00000U && phys <= 0x05CFFFFFU) || (phys >= 0x05D00000U && phys <= 0x05DFFFFFU) ||
         (phys >= 0x05F00000U && phys <= 0x05FFFFFFU);
}

} // namespace saturnis::mem
