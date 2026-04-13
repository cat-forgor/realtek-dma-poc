// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <expected>
#include <string_view>
#include <utility>

enum class Error {
  NicNotFound,
  RegistryFail,
  DeviceOpenFail,
  IoctlFail,
  ResponseTooShort,
  MmioMapFail,
  PrivilegeFail,
  PhysAllocFail,
  VirtualAllocFail,
  PageMapFail,
  DmaInitFail,
  DmaTimeout,
  BufferTooLarge,
  ThreadHijackFail,
  ProcessNotFound,
  PeParseFail,
  PageTableWalkFail,
};

[[nodiscard]] constexpr std::string_view to_string(Error e) {
  using enum Error;
  switch (e) {
  case NicNotFound:
    return "Realtek NIC not found";
  case RegistryFail:
    return "Registry operation failed";
  case DeviceOpenFail:
    return "Failed to open control device";
  case IoctlFail:
    return "IOCTL failed";
  case ResponseTooShort:
    return "Response too short";
  case MmioMapFail:
    return "MMIO mapping failed";
  case PrivilegeFail:
    return "Failed to acquire SeLockMemoryPrivilege";
  case PhysAllocFail:
    return "AllocateUserPhysicalPages failed";
  case VirtualAllocFail:
    return "VirtualAlloc MEM_PHYSICAL failed";
  case PageMapFail:
    return "MapUserPhysicalPages failed";
  case DmaInitFail:
    return "DMA engine init failed";
  case DmaTimeout:
    return "DMA transfer timed out";
  case BufferTooLarge:
    return "Buffer exceeds page size";
  case ThreadHijackFail:
    return "Thread hijack failed";
  case ProcessNotFound:
    return "Target process not found";
  case PeParseFail:
    return "PE parsing failed";
  case PageTableWalkFail:
    return "Page table walk failed";
  }
  std::unreachable(); // :sob:
}

template <typename T = void> using Result = std::expected<T, Error>;

using Void = Result<>;
