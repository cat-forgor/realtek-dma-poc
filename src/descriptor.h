// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "nic.h"

struct alignas(16) TxDescriptor {
  uint32_t opts1{};
  uint32_t opts2{};
  uint32_t buf_lo{};
  uint32_t buf_hi{};

  static constexpr auto make(uint64_t buf_pa, uint32_t len) -> TxDescriptor {
    return {
        .opts1 = Nic::Desc::OWN | Nic::Desc::EOR | Nic::Desc::FS |
                 Nic::Desc::LS | (len & 0x3FFF),
        .opts2 = 0,
        .buf_lo = static_cast<uint32_t>(buf_pa),
        .buf_hi = static_cast<uint32_t>(buf_pa >> 32),
    };
  }

  [[nodiscard]] auto owned_by_nic() const volatile -> bool {
    return (opts1 & Nic::Desc::OWN) != 0;
  }
};

static_assert(sizeof(TxDescriptor) == 16);

// Layout:
//   [0x00] QWORD  buf_addr (physical address)
//   [0x08] DWORD  padding  (= 0)
//   [0x0C] DWORD  opts1    (OWN, EOR, buf_size)
struct alignas(16) RxDescriptor {
  uint32_t buf_lo{};   // 0x00
  uint32_t buf_hi{};   // 0x04
  uint32_t reserved{}; // 0x08
  uint32_t opts1{};    // 0x0C

  static constexpr auto make(uint64_t buf_pa, uint32_t buf_size = 4096)
      -> RxDescriptor {
    return {
        .buf_lo = static_cast<uint32_t>(buf_pa),
        .buf_hi = static_cast<uint32_t>(buf_pa >> 32),
        .reserved = 0,
        .opts1 = Nic::Desc::OWN | Nic::Desc::EOR | (buf_size & 0x3FFF),
    };
  }

  [[nodiscard]] auto owned_by_nic() const volatile -> bool {
    return (opts1 & Nic::Desc::OWN) != 0;
  }
};

static_assert(sizeof(RxDescriptor) == 16);

// OID req buf (sent over IOCTL)
struct OidRequest {
  uint32_t oid_code{};
  uint32_t reserved{};
  std::array<uint8_t, 256> data{};
};

struct PhysPage {
  void *va{};
  uint64_t pa{};

  [[nodiscard]] explicit operator bool() const { return va != nullptr; }

  template <typename T> [[nodiscard]] auto as() const -> T * {
    return static_cast<T *>(va);
  }

  void clear(uint8_t val = 0) const {
    if (va)
      std::memset(va, val, 4096);
  }
};
