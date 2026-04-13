// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <chrono>
#include <cstring>
#include <print>
#include <span>
#include <thread>
#include <utility>

#include "descriptor.h"
#include "error.h"
#include "mmio.h"
#include "nic.h"
#include "phys_alloc.h"
#include <Windows.h>

struct NicState {
  uint8_t cmd{};
  uint32_t tx_cfg{};
  uint32_t rx_cfg{};
  uint64_t tnpds{};
  uint64_t rdsar{};

  [[nodiscard]] static auto capture(MmioAccess const &mmio) -> NicState {
    return {
        .cmd = mmio.read<uint8_t>(Nic::Reg::CMD),
        .tx_cfg = mmio.read<uint32_t>(Nic::Reg::TX_CONFIG),
        .rx_cfg = mmio.read<uint32_t>(Nic::Reg::RX_CONFIG),
        .tnpds = mmio.read64(Nic::Reg::TNPDS_LO),
        .rdsar = mmio.read64(Nic::Reg::RDSAR_LO),
    };
  }

  void restore_to(MmioAccess const &mmio) const {
    using namespace std::chrono_literals;
    mmio.write<uint32_t>(Nic::Reg::IMR_DISABLE, 0xFDFF'FFFF);

    mmio.write<uint8_t>(Nic::Reg::CMD, 0);
    std::this_thread::sleep_for(10ms);

    mmio.write64(Nic::Reg::TNPDS_LO, tnpds);
    mmio.write64(Nic::Reg::RDSAR_LO, rdsar);
    mmio.write<uint32_t>(Nic::Reg::TX_CONFIG, tx_cfg);
    mmio.write<uint32_t>(Nic::Reg::RX_CONFIG, rx_cfg);
    mmio.write<uint8_t>(Nic::Reg::CMD, cmd);

    mmio.write<uint32_t>(Nic::Reg::IMR_ENABLE, 0x0021'0001);
  }
};

// Page layout:
//   [0] TX descriptor ring (single descriptor)
//   [1] RX descriptor ring (single descriptor)
//   [2] TX data buffer
//   [3] RX data buffer
class DmaEngine {
  MmioAccess mmio_;
  PhysAllocator phys_;
  NicState saved_{};

  static constexpr ULONG_PTR PAGE_TX_DESC = 0;
  static constexpr ULONG_PTR PAGE_RX_DESC = 1;
  static constexpr ULONG_PTR PAGE_TX_BUF = 2;
  static constexpr ULONG_PTR PAGE_RX_BUF = 3;
  static constexpr ULONG_PTR PAGES_NEEDED = 4;

  static constexpr auto MAX_WAIT = std::chrono::milliseconds{2};
  static constexpr auto POLL_INTERVAL = std::chrono::microseconds{10};

  [[nodiscard]] auto tx_desc() const -> volatile TxDescriptor * {
    return phys_[PAGE_TX_DESC].as<TxDescriptor>();
  }

  [[nodiscard]] auto rx_desc() const -> volatile RxDescriptor * {
    return phys_[PAGE_RX_DESC].as<RxDescriptor>();
  }

  template <typename Desc>
  auto wait_desc_complete(volatile Desc *desc) const -> bool {
    MemoryBarrier();
    if (!desc->owned_by_nic())
      return true;

    for (int i = 0; i < 2000; ++i) {
      YieldProcessor();
      MemoryBarrier();
      if (!desc->owned_by_nic())
        return true;
    }

    auto deadline = std::chrono::steady_clock::now() + MAX_WAIT;
    while (std::chrono::steady_clock::now() < deadline) {
      YieldProcessor();
      MemoryBarrier();
      if (!desc->owned_by_nic())
        return true;
    }
    return false;
  }

  DmaEngine(MmioAccess mmio, PhysAllocator phys)
      : mmio_{mmio}, phys_{std::move(phys)} {}

public:
  [[nodiscard]] static auto create(void *mmio_base) -> Result<DmaEngine> {
    auto phys = PhysAllocator::create(PAGES_NEEDED);
    if (!phys)
      return std::unexpected{phys.error()};

    auto engine = DmaEngine{MmioAccess{mmio_base}, std::move(*phys)};

    for (ULONG_PTR i = 0; i < PAGES_NEEDED; ++i) {
      auto p = engine.phys_[i];
      std::println("    page[{}]: VA={} PA={:#x}", i, p.va, p.pa);
    }

    return engine;
  }

  void save_and_setup_loopback() {
    using namespace std::chrono_literals;
    saved_ = NicState::capture(mmio_);

    std::println("[+] NIC state saved (CMD={:#04x} TX={:#010x} RX={:#010x})",
                 saved_.cmd, saved_.tx_cfg, saved_.rx_cfg);
    std::println("    TNPDS={:#018x} RDSAR={:#018x}", saved_.tnpds,
                 saved_.rdsar);

    // Disable interrupts so ISR doesn't fire
    mmio_.write<uint32_t>(Nic::Reg::IMR_DISABLE, 0xFDFF'FFFF);
    mmio_.write<uint32_t>(Nic::Reg::ISR, 0xFFFF'FFFF);

    // Reset the chip to make sure RSDAR is picked up on restart
    mmio_.write<uint8_t>(Nic::Reg::CMD, Nic::Cmd::RST);
    for (int i = 0; i < 100; ++i) {
      std::this_thread::sleep_for(10ms);
      if (!(mmio_.read<uint8_t>(Nic::Reg::CMD) & Nic::Cmd::RST))
        break;
    }
    std::println("    Reset complete (CMD={:#04x})",
                 mmio_.read<uint8_t>(Nic::Reg::CMD));

    mmio_.write<uint32_t>(Nic::Reg::IMR_DISABLE, 0xFDFF'FFFF);
    mmio_.write<uint32_t>(Nic::Reg::ISR, 0xFFFF'FFFF);

    mmio_.write64(Nic::Reg::TNPDS_LO, phys_[PAGE_TX_DESC].pa);
    mmio_.write64(Nic::Reg::RDSAR_LO, phys_[PAGE_RX_DESC].pa);

    auto tnpds_rb = mmio_.read64(Nic::Reg::TNPDS_LO);
    auto rdsar_rb = mmio_.read64(Nic::Reg::RDSAR_LO);
    std::println("    TNPDS write={:#018x} read={:#018x}",
                 phys_[PAGE_TX_DESC].pa, tnpds_rb);
    std::println("    RDSAR write={:#018x} read={:#018x}",
                 phys_[PAGE_RX_DESC].pa, rdsar_rb);

    // Restore TX/RX config with loopback & accept all
    mmio_.write<uint32_t>(Nic::Reg::TX_CONFIG,
                          saved_.tx_cfg | Nic::TX_CFG_LOOPBACK);
    mmio_.write<uint32_t>(Nic::Reg::RX_CONFIG,
                          saved_.rx_cfg | Nic::RX_CFG_ACCEPT_ALL);

    // Enable TX + RX
    mmio_.write<uint8_t>(Nic::Reg::CMD, Nic::Cmd::TE | Nic::Cmd::RE);
    std::this_thread::sleep_for(10ms);

    std::println(
        "[+] NIC in loopback mode (CMD={:#04x} TX={:#010x} RX={:#010x})",
        mmio_.read<uint8_t>(Nic::Reg::CMD),
        mmio_.read<uint32_t>(Nic::Reg::TX_CONFIG),
        mmio_.read<uint32_t>(Nic::Reg::RX_CONFIG));
  }

  void refresh_loopback() {
    using namespace std::chrono_literals;

    // Disable TX/RX without full reset
    mmio_.write<uint8_t>(Nic::Reg::CMD, 0);

    std::this_thread::sleep_for(1ms);
    mmio_.write<uint32_t>(Nic::Reg::IMR_DISABLE, 0xFDFF'FFFF);
    mmio_.write<uint32_t>(Nic::Reg::ISR, 0xFFFF'FFFF);
    mmio_.write64(Nic::Reg::TNPDS_LO, phys_[PAGE_TX_DESC].pa);
    mmio_.write64(Nic::Reg::RDSAR_LO, phys_[PAGE_RX_DESC].pa);

    mmio_.write<uint32_t>(Nic::Reg::TX_CONFIG,
                          saved_.tx_cfg | Nic::TX_CFG_LOOPBACK);
    mmio_.write<uint32_t>(Nic::Reg::RX_CONFIG,
                          saved_.rx_cfg | Nic::RX_CFG_ACCEPT_ALL);

    // Reenable TX & RX
    mmio_.write<uint8_t>(Nic::Reg::CMD, Nic::Cmd::TE | Nic::Cmd::RE);
    std::this_thread::sleep_for(1ms);
  }

  void restore() {
    saved_.restore_to(mmio_);
    std::println("[+] NIC state restored");
  }

  // this is all for RTL8125B
  [[nodiscard]] auto try_doorbell(const char *name, auto doorbell_fn) -> bool {
    using namespace std::chrono_literals;

    auto *tx_buf = static_cast<uint8_t *>(phys_[PAGE_TX_BUF].va);
    for (int i = 0; i < 64; ++i)
      tx_buf[i] = static_cast<uint8_t>(0xAA ^ i);

    auto td = TxDescriptor::make(phys_[PAGE_TX_BUF].pa, 64);
    std::memcpy(const_cast<TxDescriptor *>(tx_desc()), &td, sizeof(td));

    phys_[PAGE_RX_BUF].clear();
    auto rd = RxDescriptor::make(phys_[PAGE_RX_BUF].pa);
    std::memcpy(const_cast<RxDescriptor *>(rx_desc()), &rd, sizeof(rd));

    mmio_.write<uint32_t>(Nic::Reg::ISR, 0xFFFF'FFFF);
    MemoryBarrier();
    doorbell_fn();

    std::this_thread::sleep_for(100ms);

    auto isr = mmio_.read<uint32_t>(Nic::Reg::ISR);
    bool tx_done = !tx_desc()->owned_by_nic();
    bool rx_done = !rx_desc()->owned_by_nic();

    std::println("    {}: ISR={:#010x} TX_OWN={} RX_OWN={} -> {}", name, isr,
                 tx_done ? 0 : 1, rx_done ? 0 : 1,
                 (tx_done && rx_done) ? "OK" : "no response");

    // dump for testing
    auto *raw_tx = reinterpret_cast<volatile uint32_t *>(tx_desc());
    auto *raw_rx = reinterpret_cast<volatile uint32_t *>(rx_desc());
    std::println("    TX desc: {:08x} {:08x} {:08x} {:08x}",
                 (uint32_t)raw_tx[0], (uint32_t)raw_tx[1], (uint32_t)raw_tx[2],
                 (uint32_t)raw_tx[3]);
    std::println("    RX desc: {:08x} {:08x} {:08x} {:08x}",
                 (uint32_t)raw_rx[0], (uint32_t)raw_rx[1], (uint32_t)raw_rx[2],
                 (uint32_t)raw_rx[3]);

    if (tx_done && rx_done) {
      auto *rx_buf = static_cast<uint8_t *>(phys_[PAGE_RX_BUF].va);
      std::print("    RX data: ");
      for (int i = 0; i < 16; ++i)
        std::print("{:02x} ", rx_buf[i]);
      std::println("");
    }
    return tx_done && rx_done;
  }

  int doorbell_type_ = -1;

  void ring_doorbell() {
    switch (doorbell_type_) {
    case 0:
      // Legacy?
      mmio_.write<uint8_t>(Nic::Reg::TPPOLL, Nic::TPPOLL_NPQ | 0x10);
      break;
    case 1:
      // Extended?
      mmio_.write<uint16_t>(Nic::Reg::TPPOLL_EXT, 1 | 0x10);
      break;
    case 2:
      mmio_.write<uint16_t>(Nic::Reg::SW_TAIL_PTR0, 1);
      break;
    }
  }

  // ngl this could be hardcoded but wtv
  [[nodiscard]] auto self_test() -> bool {
    std::println("    TX desc ring PA={:#x}  RX desc ring PA={:#x}",
                 phys_[PAGE_TX_DESC].pa, phys_[PAGE_RX_DESC].pa);

    if (try_doorbell("TPPOLL(0x38)=0x50", [&] {
          mmio_.write<uint8_t>(Nic::Reg::TPPOLL, 0x40 | 0x10);
        })) {
      doorbell_type_ = 0;
      return true;
    }

    if (try_doorbell("TPPOLL_EXT(0x90)=0x11", [&] {
          mmio_.write<uint16_t>(Nic::Reg::TPPOLL_EXT, 1 | 0x10);
        })) {
      doorbell_type_ = 1;
      return true;
    }

    if (try_doorbell("TPPOLL_EXT(0x90) TX+RX sep", [&] {
          mmio_.write<uint16_t>(Nic::Reg::TPPOLL_EXT, 1);
          MemoryBarrier();
          mmio_.write<uint16_t>(Nic::Reg::TPPOLL_EXT, 0x10);
        })) {
      doorbell_type_ = 1;
      return true;
    }

    if (try_doorbell("TPPOLL_EXT(0x90)=1 (TX only, long wait)",
                     [&] { mmio_.write<uint16_t>(Nic::Reg::TPPOLL_EXT, 1); })) {
      doorbell_type_ = 1;
      return true;
    }

    if (try_doorbell("SW_TAIL(0x2800)=1", [&] {
          mmio_.write<uint16_t>(Nic::Reg::SW_TAIL_PTR0, 1);
        })) {
      doorbell_type_ = 2;
      return true;
    }

    std::println("    All doorbell methods failed");
    return false;
  }

  [[nodiscard]] auto phys_read(uint64_t src_pa, std::span<uint8_t> dst)
      -> Void {
    if (dst.size() > 4096)
      return std::unexpected{Error::BufferTooLarge};

    // goes through mac, 64 byte min frame size
    auto tx_len = static_cast<uint32_t>(dst.size() < 64 ? 64 : dst.size());
    auto td = TxDescriptor::make(src_pa, tx_len);
    std::memcpy(const_cast<TxDescriptor *>(tx_desc()), &td, sizeof(td));

    auto rd = RxDescriptor::make(phys_[PAGE_RX_BUF].pa);
    std::memcpy(const_cast<RxDescriptor *>(rx_desc()), &rd, sizeof(rd));

    MemoryBarrier();
    ring_doorbell();

    // RX completes after TX so we can just wait for RX
    if (!wait_desc_complete(rx_desc()))
      return std::unexpected{Error::DmaTimeout};

    std::memcpy(dst.data(), phys_[PAGE_RX_BUF].va, dst.size());
    return {};
  }

  [[nodiscard]] auto phys_write(uint64_t dst_pa, std::span<const uint8_t> src)
      -> Void {
    if (src.size() > 4096)
      return std::unexpected{Error::BufferTooLarge};

    // goes through mac, 64 byte min frame size
    auto tx_len = static_cast<uint32_t>(src.size() < 64 ? 64 : src.size());

    if (src.size() < 64)
      std::memset(phys_[PAGE_TX_BUF].va, 0, 64);
    std::memcpy(phys_[PAGE_TX_BUF].va, src.data(), src.size());

    auto td = TxDescriptor::make(phys_[PAGE_TX_BUF].pa, tx_len);
    std::memcpy(const_cast<TxDescriptor *>(tx_desc()), &td, sizeof(td));

    auto rd = RxDescriptor::make(dst_pa);
    std::memcpy(const_cast<RxDescriptor *>(rx_desc()), &rd, sizeof(rd));

    MemoryBarrier();
    ring_doorbell();

    if (!wait_desc_complete(tx_desc()))
      return std::unexpected{Error::DmaTimeout};
    if (!wait_desc_complete(rx_desc()))
      return std::unexpected{Error::DmaTimeout};

    return {};
  }
};
