#pragma once

#include "core/time.hpp"

#include <cstdint>
#include <string_view>

namespace saturnis::bus {

enum class BusKind { IFetch, Read, Write, MmioRead, MmioWrite, Barrier };

enum class BusProducer { Auto, Cpu, Dma };

struct BusOp {
  int cpu_id = 0;
  core::Tick req_time = 0;
  std::uint64_t sequence = 0;
  BusKind kind = BusKind::Read;
  std::uint32_t phys_addr = 0;
  std::uint8_t size = 4;
  std::uint32_t data = 0;
  bool fill_cache_line = false;
  std::uint8_t cache_line_size = 0;
  BusProducer producer = BusProducer::Auto;
  std::uint64_t producer_token = 0;
};

inline std::string_view kind_name(BusKind kind) {
  switch (kind) {
  case BusKind::IFetch:
    return "IFETCH";
  case BusKind::Read:
    return "READ";
  case BusKind::Write:
    return "WRITE";
  case BusKind::MmioRead:
    return "MMIO_READ";
  case BusKind::MmioWrite:
    return "MMIO_WRITE";
  case BusKind::Barrier:
    return "BARRIER";
  }
  return "UNKNOWN";
}

inline std::string_view owner_name(const BusOp &op) {
  if (op.producer == BusProducer::Dma || (op.producer == BusProducer::Auto && op.cpu_id < 0)) {
    return "DMA";
  }
  return "CPU";
}

inline std::string_view provenance_tag(const BusOp &op) {
  if (op.producer == BusProducer::Dma || (op.producer == BusProducer::Auto && op.cpu_id < 0)) {
    return "DMA";
  }
  return "CPU";
}

inline std::string_view source_name(const BusOp &op) {
  if (op.producer == BusProducer::Dma || (op.producer == BusProducer::Auto && op.cpu_id < 0)) {
    return "DMA";
  }
  switch (op.kind) {
  case BusKind::IFetch:
    return "IFETCH";
  case BusKind::Read:
    return "READ";
  case BusKind::Write:
    return "WRITE";
  case BusKind::MmioRead:
  case BusKind::MmioWrite:
    return "MMIO";
  case BusKind::Barrier:
    return "BARRIER";
  }
  return "UNKNOWN";
}

} // namespace saturnis::bus
