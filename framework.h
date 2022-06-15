#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
// Windows Header Files
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d9types.h> // for D3DFORMAT
#include <shellapi.h>

// Fix gdiplustypes.h requires min/max
#include <algorithm>
using std::min;
using std::max;

#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)

// C RunTime Header Files
#include <filesystem>
#include <optional>
#include <unordered_set>
#include <span>
