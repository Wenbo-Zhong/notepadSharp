#pragma once
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
// Owned by caller. Icons are rendered from vector geometry at the target DPI.
HIMAGELIST createToolbarIcons(UINT dpi);
