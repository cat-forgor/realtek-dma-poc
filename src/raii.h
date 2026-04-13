// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <Windows.h>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

struct HandleDeleter {
  using pointer = HANDLE;
  void operator()(HANDLE h) const noexcept {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};

using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

[[nodiscard]] inline UniqueHandle wrap_handle(HANDLE h) {
  return UniqueHandle{(h == INVALID_HANDLE_VALUE) ? nullptr : h};
}

struct RegKeyDeleter {
  using pointer = HKEY;
  void operator()(HKEY k) const noexcept {
    if (k)
      RegCloseKey(k);
  }
};

using UniqueRegKey =
    std::unique_ptr<std::remove_pointer_t<HKEY>, RegKeyDeleter>;

[[nodiscard]] inline UniqueRegKey open_reg_key(HKEY parent, const wchar_t *path,
                                               REGSAM access) {
  HKEY key{};
  if (RegOpenKeyExW(parent, path, 0, access, &key) != ERROR_SUCCESS)
    return {};
  return UniqueRegKey{key};
}

[[nodiscard]] inline auto narrow(std::wstring_view ws) -> std::string {
  std::string s(ws.size(), '\0');
  std::ranges::transform(ws, s.begin(),
                         [](wchar_t c) { return static_cast<char>(c); });
  return s;
}