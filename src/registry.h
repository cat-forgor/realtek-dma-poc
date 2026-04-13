// Copyright (c) 2026 Evie Fogarty. All rights reserved.

#pragma once

#include <string>

#include "error.h"
#include <Windows.h>

[[nodiscard]] auto find_realtek_instance_id() -> Result<std::wstring>;
[[nodiscard]] auto set_drv_mode(std::wstring_view instance_id) -> bool;
