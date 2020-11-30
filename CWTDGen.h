#pragma once

#include "resource.h"

namespace fs = std::filesystem;
namespace Gp = Gdiplus;

constexpr uint32_t TextureWidth = 4096;
constexpr uint32_t TextureHeight = 4096;
constexpr uint32_t CharWidth = 64;
constexpr uint32_t CharHeight = 80;
constexpr uint32_t TextureXChars = TextureWidth / CharWidth;
constexpr uint32_t TextureYChars = TextureHeight / CharHeight;
static const std::unordered_set<wchar_t> SymbolSet = { L'—', L'　', L'、', L'。', L'《', L'》', L'「', L'」', L'『', L'』', L'！', L'，', L'－', L'．', L'：', L'；', L'？', L'～' };
static const std::unordered_set<wchar_t> IgnoreSet = { L'\n', L'\r' };
static const std::unordered_map<wchar_t, wchar_t> ReplaceMap = { {L'「', L'“'}, {L'」', L'”'}, {L'『', L'‘'}, {L'』', L'’'} };

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
