// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <utility>
#include <vector>

#include "descriptor.h"
#include "error.h"
#include "raii.h"
#include <Windows.h>

#pragma comment(lib, "advapi32.lib")

class PhysAllocator {
  std::vector<ULONG_PTR> pfns_;
  ULONG_PTR count_{};
  void *va_{};

  PhysAllocator(std::vector<ULONG_PTR> pfns, ULONG_PTR count, void *va)
      : pfns_{std::move(pfns)}, count_{count}, va_{va} {}

public:
  PhysAllocator(const PhysAllocator &) = delete;
  PhysAllocator &operator=(const PhysAllocator &) = delete;

  PhysAllocator(PhysAllocator &&o) noexcept
      : pfns_{std::move(o.pfns_)}, count_{std::exchange(o.count_, 0)},
        va_{std::exchange(o.va_, nullptr)} {}

  PhysAllocator &operator=(PhysAllocator &&o) noexcept {
    if (this != &o) {
      release();
      pfns_ = std::move(o.pfns_);
      count_ = std::exchange(o.count_, 0);
      va_ = std::exchange(o.va_, nullptr);
    }
    return *this;
  }

  ~PhysAllocator() { release(); }

  [[nodiscard]] static auto create(ULONG_PTR page_count)
      -> Result<PhysAllocator> {
    if (auto token = wrap_handle([] {
          HANDLE t{};
          OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &t);
          return t;
        }())) {
      TOKEN_PRIVILEGES tp{.PrivilegeCount = 1};
      LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME,
                           &tp.Privileges[0].Luid);
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
      AdjustTokenPrivileges(token.get(), FALSE, &tp, sizeof(tp), nullptr,
                            nullptr);
    } else {
      return std::unexpected{Error::PrivilegeFail};
    }

    auto pfns = std::vector<ULONG_PTR>(page_count);
    auto count = page_count;

    if (!AllocateUserPhysicalPages(GetCurrentProcess(), &count, pfns.data()))
      return std::unexpected{Error::PhysAllocFail};

    auto *va = VirtualAlloc(nullptr, count * 4096, MEM_RESERVE | MEM_PHYSICAL,
                            PAGE_READWRITE);
    if (!va) {
      FreeUserPhysicalPages(GetCurrentProcess(), &count, pfns.data());
      return std::unexpected{Error::VirtualAllocFail};
    }

    if (!MapUserPhysicalPages(va, count, pfns.data())) {
      VirtualFree(va, 0, MEM_RELEASE);
      FreeUserPhysicalPages(GetCurrentProcess(), &count, pfns.data());
      return std::unexpected{Error::PageMapFail};
    }

    return PhysAllocator{std::move(pfns), count, va};
  }

  [[nodiscard]] auto operator[](ULONG_PTR idx) const -> PhysPage {
    [[assume(idx < count_)]];
    return {
        .va = static_cast<uint8_t *>(va_) + idx * 4096,
        .pa = pfns_[idx] * 4096ULL,
    };
  }

  [[nodiscard]] auto size() const -> ULONG_PTR { return count_; }

private:
  void release() {
    if (va_) {
      MapUserPhysicalPages(va_, count_, nullptr);
      VirtualFree(va_, 0, MEM_RELEASE);
    }
    if (!pfns_.empty()) {
      FreeUserPhysicalPages(GetCurrentProcess(), &count_, pfns_.data());
    }
    va_ = nullptr;
    pfns_.clear();
    count_ = 0;
  }
};
