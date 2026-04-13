// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#include "registry.h"

#include <algorithm>
#include <array>
#include <format>
#include <print>
#include <string>
#include <string_view>

#include "nic.h"
#include "raii.h"
#include <Windows.h>

auto find_realtek_instance_id() -> Result<std::wstring> {
  auto net_key =
      open_reg_key(HKEY_LOCAL_MACHINE, Nic::NET_CLASS_GUID.data(), KEY_READ);
  if (!net_key)
    return std::unexpected{Error::RegistryFail};

  for (DWORD i = 0;; ++i) {
    std::array<wchar_t, 256> subkey{};
    DWORD len = static_cast<DWORD>(subkey.size());

    if (RegEnumKeyExW(net_key.get(), i, subkey.data(), &len, nullptr, nullptr,
                      nullptr, nullptr) != ERROR_SUCCESS)
      break;

    auto adapter = open_reg_key(net_key.get(), subkey.data(), KEY_READ);
    if (!adapter)
      continue;

    std::array<wchar_t, 512> desc{};
    DWORD desc_sz = sizeof(desc);
    RegQueryValueExW(adapter.get(), L"DriverDesc", nullptr, nullptr,
                     reinterpret_cast<BYTE *>(desc.data()), &desc_sz);

    auto desc_view = std::wstring_view{desc.data()};
    auto is_realtek = std::ranges::any_of(Nic::KNOWN_DESCS, [&](auto prefix) {
      return desc_view.find(prefix) != std::wstring_view::npos;
    });

    if (!is_realtek)
      continue;

    std::array<wchar_t, 128> guid{};
    DWORD guid_sz = sizeof(guid);
    if (RegQueryValueExW(adapter.get(), L"NetCfgInstanceId", nullptr, nullptr,
                         reinterpret_cast<BYTE *>(guid.data()),
                         &guid_sz) == ERROR_SUCCESS) {
      return std::wstring{guid.data()};
    }
  }

  return std::unexpected{Error::NicNotFound};
}

auto set_drv_mode(std::wstring_view instance_id) -> bool {
  auto net_key =
      open_reg_key(HKEY_LOCAL_MACHINE, Nic::NET_CLASS_GUID.data(), KEY_READ);
  if (!net_key)
    return false;

  std::wstring service_name;
  for (DWORD i = 0;; ++i) {
    std::array<wchar_t, 256> subkey{};
    DWORD len = static_cast<DWORD>(subkey.size());

    if (RegEnumKeyExW(net_key.get(), i, subkey.data(), &len, nullptr, nullptr,
                      nullptr, nullptr) != ERROR_SUCCESS)
      break;

    auto full_path = std::format(L"{}\\{}", Nic::NET_CLASS_GUID, subkey.data());
    auto adapter =
        open_reg_key(HKEY_LOCAL_MACHINE, full_path.c_str(), KEY_READ);
    if (!adapter)
      continue;

    std::array<wchar_t, 128> guid{};
    DWORD guid_sz = sizeof(guid);
    RegQueryValueExW(adapter.get(), L"NetCfgInstanceId", nullptr, nullptr,
                     reinterpret_cast<BYTE *>(guid.data()), &guid_sz);

    if (instance_id != guid.data())
      continue;

    std::array<wchar_t, 128> svc{};
    DWORD svc_sz = sizeof(svc);
    if (RegQueryValueExW(adapter.get(), L"Service", nullptr, nullptr,
                         reinterpret_cast<BYTE *>(svc.data()),
                         &svc_sz) == ERROR_SUCCESS) {
      service_name = svc.data();
    } else {
      // microsoft moment
      std::array<wchar_t, 256> dev_id{};
      DWORD dev_id_sz = sizeof(dev_id);
      if (RegQueryValueExW(adapter.get(), L"DeviceInstanceID", nullptr, nullptr,
                           reinterpret_cast<BYTE *>(dev_id.data()),
                           &dev_id_sz) == ERROR_SUCCESS) {
        auto enum_path =
            std::format(L"SYSTEM\\CurrentControlSet\\Enum\\{}", dev_id.data());
        auto enum_key =
            open_reg_key(HKEY_LOCAL_MACHINE, enum_path.c_str(), KEY_READ);
        if (enum_key) {
          svc_sz = sizeof(svc);
          if (RegQueryValueExW(enum_key.get(), L"Service", nullptr, nullptr,
                               reinterpret_cast<BYTE *>(svc.data()),
                               &svc_sz) == ERROR_SUCCESS) {
            service_name = svc.data();
          }
        }
      }
    }
    break;
  }

  if (service_name.empty()) {
    std::println(stderr,
                 "[-] Could not find driver service name"); // skill issue
    return false;
  }

  std::println("[*] Driver service: {}", narrow(service_name));

  // reads DrvMode from its wdf param key
  auto params_path = std::format(
      L"SYSTEM\\CurrentControlSet\\Services\\{}\\Parameters", service_name);
  auto params_key = open_reg_key(HKEY_LOCAL_MACHINE, params_path.c_str(),
                                 KEY_READ | KEY_WRITE); // what could go wrong?
  if (!params_key) {
    std::println(stderr, "[-] Cannot open Parameters key (error {})",
                 GetLastError());
    return false;
  }

  bool changed = false;

  // bit 2 gates MMIO mapping OID
  DWORD drv_mode = 0;
  DWORD drv_mode_sz = sizeof(drv_mode);
  RegQueryValueExW(params_key.get(), L"DrvMode", nullptr, nullptr,
                   reinterpret_cast<BYTE *>(&drv_mode), &drv_mode_sz);

  if (!(drv_mode & 4)) {
    drv_mode |= 4;
    RegSetValueExW(params_key.get(), L"DrvMode", 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&drv_mode), sizeof(drv_mode));
    changed = true;
  }

  DWORD dma_remap = 1;
  DWORD dma_remap_sz = sizeof(dma_remap);
  RegQueryValueExW(params_key.get(), L"DmaRemappingCompatible", nullptr,
                   nullptr, reinterpret_cast<BYTE *>(&dma_remap),
                   &dma_remap_sz);

  // fuck u iommu
  if (dma_remap != 0) {
    DWORD zero = 0; // type shit
    RegSetValueExW(params_key.get(), L"DmaRemappingCompatible", 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&zero), sizeof(zero));
    changed = true;
  }

  return changed; // ugghhhh
}
