#pragma once

#include "resource.h"

namespace fs = std::filesystem;
namespace Gp = Gdiplus;

constexpr uint32_t TEXTURE_WIDTH = 4096;
constexpr uint32_t TEXTURE_HEIGHT = 4096;
constexpr int CHAR_WIDTH = 64;
constexpr int CHAR_HEIGHT = 80;
constexpr uint32_t TEXTURE_XCHARS = TEXTURE_WIDTH / CHAR_WIDTH;
constexpr uint32_t TEXTURE_YCHARS = TEXTURE_HEIGHT / CHAR_HEIGHT;
static const std::unordered_set<wchar_t> SYMBOL_SET = { L'—', L'　', L'、', L'。', L'《', L'》', L'「', L'」', L'『', L'』', L'！', L'，', L'－', L'．', L'：', L'；', L'？', L'～' };
static const std::unordered_set<wchar_t> IGNORE_SET = { L'\n', L'\r' };

HINSTANCE g_hInst;
fs::path g_exePath;
wil::com_ptr<ID2D1Factory> g_d2dFactory;
wil::com_ptr<IDWriteFactory> g_dwriteFactory;
LOGFONTW g_font = {};
LOGFONTW g_symbolFont = {};
fs::path g_gamePath;

#include "Util.hpp"
#include "Graphics.hpp"
#include "RageUtil.hpp"
