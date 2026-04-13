// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "nic.h"

class MmioAccess {
  volatile std::byte *base_{};

public:
  constexpr MmioAccess() = default;
  explicit MmioAccess(void *base)
      : base_{static_cast<volatile std::byte *>(base)} {}

  [[nodiscard]] explicit operator bool() const { return base_ != nullptr; }

  template <std::unsigned_integral T>
  [[nodiscard]] auto read(uint16_t off) const -> T {
    return *reinterpret_cast<volatile T *>(base_ + off);
  }

  template <std::unsigned_integral T> void write(uint16_t off, T val) const {
    *reinterpret_cast<volatile T *>(base_ + off) = val;
  }

  void write64(uint16_t off, uint64_t val) const {
    write<uint32_t>(off, static_cast<uint32_t>(val));
    write<uint32_t>(off + 4, static_cast<uint32_t>(val >> 32));
  }

  [[nodiscard]] auto read64(uint16_t off) const -> uint64_t {
    return read<uint32_t>(off) |
           (static_cast<uint64_t>(read<uint32_t>(off + 4)) << 32);
  }

  [[nodiscard]] auto mac_address() const -> std::array<uint8_t, 6> {
    std::array<uint8_t, 6> mac;
    for (auto i : {0, 1, 2, 3, 4, 5})
      mac[i] = read<uint8_t>(Nic::Reg::MAC0 + static_cast<uint16_t>(i));
    return mac;
  }
};
