// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// RTL8125B
namespace Nic {

inline constexpr uint32_t IOCTL_OID = 0x0012'C818;

inline constexpr uint32_t OID_HANDSHAKE =
    0xFF81'3906; // what?? unaligned too??????????
inline constexpr uint32_t HANDSHAKE_MAGIC =
    0x8168'8169; // why the fuck does thiks contain the chip id

inline constexpr uint32_t OID_MAP_MMIO =
    0xFF81'6871; // errrmm thanks for mmio guys..

// ISR v2??
namespace Reg {
inline constexpr uint16_t MAC0 = 0x00;
inline constexpr uint16_t TNPDS_LO = 0x20;
inline constexpr uint16_t TNPDS_HI = 0x24;
inline constexpr uint16_t CMD = 0x37;
inline constexpr uint16_t TPPOLL = 0x38;
inline constexpr uint16_t ISR = 0x3C;
inline constexpr uint16_t TX_CONFIG = 0x40;
inline constexpr uint16_t RX_CONFIG = 0x44;
inline constexpr uint16_t TPPOLL_EXT = 0x90;
inline constexpr uint16_t RDSAR_LO = 0xE4;
inline constexpr uint16_t RDSAR_HI = 0xE8;
inline constexpr uint16_t SW_TAIL_PTR0 = 0x2800;
inline constexpr uint16_t IMR_DISABLE = 0xD00; // load bearing btw!
inline constexpr uint16_t IMR_ENABLE = 0xD0C;
} // namespace Reg

namespace Cmd {
inline constexpr uint8_t RST = 0x10;
inline constexpr uint8_t RE = 0x08;
inline constexpr uint8_t TE = 0x04;
} // namespace Cmd

inline constexpr uint8_t TPPOLL_NPQ = 0x40;
inline constexpr uint32_t TX_CFG_LOOPBACK = 0x0002'0000;
inline constexpr uint32_t RX_CFG_ACCEPT_ALL = 0x0F;

// Descriptor flags
namespace Desc {
inline constexpr uint32_t OWN = 1u << 31;
inline constexpr uint32_t EOR = 1u << 30;
inline constexpr uint32_t FS = 1u << 29;
inline constexpr uint32_t LS = 1u << 28;
} // namespace Desc

inline constexpr std::wstring_view NET_CLASS_GUID =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
    L"{4d36e972-e325-11ce-bfc1-08002be10318}";

inline constexpr std::array KNOWN_DESCS = {L"Realtek", L"Killer E"};

} // namespace Nic
