#pragma once

#include "resource.h"

namespace fs = std::filesystem;
namespace Gp = Gdiplus;

constexpr uint32_t TextureWidth = 4096;
constexpr uint32_t TextureHeight = 4096;
constexpr uint32_t CharWidth = 64;
constexpr uint32_t CharHeight = 66;
constexpr uint32_t TextureXChars = TextureWidth / CharWidth;
constexpr uint32_t TextureYChars = TextureHeight / CharHeight;
constexpr std::pair<wchar_t, wchar_t> NonSymbolRange[] = {
	{ L'\u4E00', L'\u9FFF' }, // 中日韩统一表意文字
	{ L'\uFF10', L'\uFF19' }, // 全角0-9
	{ L'\uFF41', L'\uFF5A' }  // 全角a-z
};
static const std::unordered_set<wchar_t> IgnoreSet = { L'\n', L'\r' };
static const std::unordered_map<wchar_t, wchar_t> ReplaceMap = { {L'「', L'“'}, {L'」', L'”'}, {L'『', L'‘'}, {L'』', L'’'} };

constexpr auto FontsPathIV = LR"(pc\textures\fonts.wtd)";
constexpr auto FontsPathTBoGT = LR"(TBoGT\pc\textures\fonts.wtd)";
constexpr auto FontsPathTLAD = LR"(TLAD\pc\textures\fonts.wtd)";
constexpr auto NewFontsPathIV = FontsPathIV;
constexpr auto NewFontsPathTBoGT = FontsPathTBoGT;
constexpr auto NewFontsPathTLAD = FontsPathTLAD;
constexpr auto CharTableDatPath = LR"(plugins\GTA4.CHS\char_table.dat)";

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
