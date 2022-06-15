#include "pch.h"
#include "CWTDGen.h"

INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ [[maybe_unused]] HINSTANCE hPrevInstance,
	_In_ [[maybe_unused]] LPWSTR    lpCmdLine,
	_In_ [[maybe_unused]] int       nCmdShow)
{
	g_hInst = hInstance;

	auto guard = wil::CoInitializeEx(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	g_exePath = GetModuleFsPath(hInstance).remove_filename();
	SetCurrentDirectoryW(g_exePath.c_str());

	THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory));
	THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(g_dwriteFactory), g_dwriteFactory.put_unknown()));

	ULONG_PTR token;
	Gp::GdiplusStartupInput input;
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(Gp::GdiplusStartup(&token, &input, nullptr)));

	return static_cast<int>(DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_DIALOG), nullptr, DialogProc));
}

std::optional<LOGFONTW> ChooseFontDialog(HWND hWndOwner, int fontPointSize)
{
	LOGFONTW lf = {
		.lfHeight = fontPointSize,
		.lfWeight = FW_BOLD,
		.lfCharSet = GB2312_CHARSET
	};
	CHOOSEFONTW cf = {
		.lStructSize = sizeof(cf),
		.hwndOwner = hWndOwner,
		.lpLogFont = &lf,
		.Flags = CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_SELECTSCRIPT,
	};
	if (ChooseFontW(&cf))
		return lf;
	return std::nullopt;
}

bool CheckGTAIVFiles(const fs::path& path)
{
	constexpr const wchar_t* list[] = {
		FontsPathIV,
		FontsPathTBoGT,
		FontsPathTLAD
	};
	for (const auto& p : list)
	{
		if (!fs::is_regular_file(path / p))
			return false;
	}
	return true;
}

bool CheckAndFixGTAIVPath(fs::path& path)
{
	if (!CheckGTAIVFiles(path))
	{
		path /= L"GTAIV";
		return CheckGTAIVFiles(path);
	}
	return true;
}

bool CheckCHSFiles(const fs::path& path)
{
	return fs::is_regular_file(path / CharTableDatPath);
}

bool CheckFontSelected(HWND hWnd)
{
	if (!g_font.lfFaceName[0] || !g_symbolFont.lfFaceName[0])
	{
		TaskDialog(hWnd, nullptr, L"CWTDGen", nullptr, L"尚未选择字体", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
		return false;
	}
	return true;
}

bool CheckGamePathSelected(HWND hWnd)
{
	if (g_gamePath.empty())
	{
		TaskDialog(hWnd, nullptr, L"CWTDGen", nullptr, L"尚未选择游戏目录", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
		return false;
	}
	return true;
}

bool CheckCharTableDat(HWND hWnd)
{
	if (!CheckCHSFiles(g_gamePath))
	{
		TaskDialog(hWnd, nullptr, L"CWTDGen", nullptr, L"游戏目录下找不到 char_table.dat，请确认已安装汉化补丁。", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
		return false;
	}
	return true;
}

void UpdatePreview(HWND hWnd, std::wstring_view text, bool useGDIP, bool replaceChars)
{
	if (text.empty())
		return;

	RECT rc;
	THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hWnd, &rc));

	const uint32_t wndWidth = rc.right - rc.left, wndHeight = rc.bottom - rc.top;
	uint32_t xMaxChars = wndWidth / CharWidth, yMaxChars = wndHeight / CharHeight;
	uint32_t xChars = static_cast<uint32_t>(text.size()), yChars = 1;
	bool requireScale = false;

	do
	{
		if (xChars > xMaxChars)
		{
			xChars = xMaxChars;
			yChars = (static_cast<uint32_t>(text.size()) + xMaxChars - 1) / xMaxChars;

			if (requireScale)
				break;

			if (yChars > yMaxChars)
			{
				requireScale = true; // calc again
				xMaxChars = static_cast<uint32_t>(ceilf(sqrtf(static_cast<float>(text.size()))));
				xChars = static_cast<uint32_t>(text.size());
			}
		}
	} while (requireScale);

	const uint32_t width = xChars * CharWidth, height = yChars * CharHeight;

	auto hdcWnd = wil::GetDC(hWnd);
	THROW_HR_IF(E_FAIL, !hdcWnd);

	HBITMAP hFinalBitmap = nullptr;
	{
		wil::unique_hdc hdc(CreateCompatibleDC(hdcWnd.get()));
		THROW_HR_IF(E_FAIL, !hdc);
		RGBQUAD* bmBits;
		wil::unique_hbitmap hBitmap(CreateDIB(hdcWnd.get(), width, height, 32, reinterpret_cast<void**>(&bmBits)));
		THROW_HR_IF(E_FAIL, !hBitmap);
		auto selectBitmap = wil::SelectObject(hdc.get(), hBitmap.get());

		GDIDrawCheckeredBackground(hdc.get(), static_cast<LONG>(width), static_cast<LONG>(height), xChars, yChars);
		SetBitmapAlpha(bmBits, width, height, 255);

		if (useGDIP)
		{
			GpDrawCharacters(hdc.get(), text, xChars, yChars, replaceChars);
		}
		else
		{
			DWriteDrawCharacters(hdc.get(), width, height, text, xChars, yChars, replaceChars);
		}

		if (requireScale)
		{
			uint32_t scaledWidth, scaledHeight;
			if (width > height)
			{
				scaledWidth = wndWidth;
				scaledHeight = height * scaledWidth / width;
			}
			else
			{
				scaledHeight = wndHeight;
				scaledWidth = width * scaledHeight / height;
			}

			auto hBitmapScale = GDIScaleBitmap(hdcWnd.get(), hdc.get(), width, height, scaledWidth, scaledHeight);
			hFinalBitmap = hBitmapScale.release();
		}
		else
			hFinalBitmap = hBitmap.release();
	}

	wil::unique_hbitmap hOldBitmap(reinterpret_cast<HBITMAP>(SendMessageW(hWnd, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(hFinalBitmap))));
}

auto GenerateCharsImage(HWND hWnd, std::wstring_view chars, bool useGDIP, bool replaceChars)
{
	auto hdcWnd = wil::GetDC(hWnd);
	THROW_HR_IF(E_FAIL, !hdcWnd);
	wil::unique_hdc hdc(CreateCompatibleDC(hdcWnd.get()));
	THROW_HR_IF(E_FAIL, !hdc);
	uint8_t* bmBits;
	wil::unique_hbitmap hBitmap(CreateDIB(hdcWnd.get(), TextureWidth, TextureHeight, 32, reinterpret_cast<void**>(&bmBits)));
	THROW_HR_IF(E_FAIL, !hBitmap);
	auto selectBitmap = wil::SelectObject(hdc.get(), hBitmap.get());

	std::fill_n(bmBits, TextureWidth * TextureHeight * 4, '\0');

	if (useGDIP)
	{
		GpDrawCharacters(hdc.get(), chars, TextureXChars, TextureYChars, replaceChars);
	}
	else
	{
		DWriteDrawCharacters(hdc.get(), TextureWidth, TextureHeight, chars, TextureXChars, TextureYChars, replaceChars);
	}

#if 0
	// image/png {557cf406-1a04-11d3-9a73-0000f81ef32e}
	static const CLSID pngEncoderClsId = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
	Gp::Bitmap(TextureWidth, TextureHeight, TextureWidth * 4, PixelFormat32bppPARGB, bmBits).Save(L"font_chs.png", &pngEncoderClsId);
#endif

	DirectX::ScratchImage dxt5Img;

	DirectX::Image img = {
		.width = TextureWidth,
		.height = TextureHeight,
		.format = DXGI_FORMAT_B8G8R8A8_UNORM,
		.rowPitch = TextureWidth * 4,
		.slicePitch = TextureWidth * TextureHeight * 4,
		.pixels = bmBits
	};
	THROW_IF_FAILED(DirectX::Compress(img, DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT, dxt5Img));

	return dxt5Img;
}

void CreateWTD(const fs::path& in, const fs::path& out, const DirectX::ScratchImage& dxt5Img)
{
	wil::unique_hfile hFile(CreateFileW(in.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	THROW_LAST_ERROR_IF(!hFile);

	auto [header, data] = RageUtil::RSC5::ReadFromFile(hFile.get());
	RageUtil::s_virtual = { data.get(), header.GetVirtualSize() };
	RageUtil::s_physical = { data.get() + RageUtil::s_virtual.size(), header.GetPhysicalSize() };

	auto dict = reinterpret_cast<RageUtil::pgDictionary<RageUtil::grcTexturePC>*>(data.get());

	auto hash = RageUtil::HashString("font_chs");

	auto texture = *dict->values.data.Get()->Get(); // copy
	texture.name.Set("pack:/font_chs.dds");
	texture.width = TextureWidth;
	texture.height = TextureHeight;
	texture.pixelFormat = D3DFMT_DXT5;
	texture.stride = TextureWidth;
	texture.next = 0;
	texture.prev = 0;
	texture.pixelData.Set(dxt5Img.GetPixels());

	std::span<uint32_t> hashes(dict->hashes.data.Get(), dict->hashes.size);
	auto it = std::find(hashes.begin(), hashes.end(), hash);

	RageUtil::pgDictionary<RageUtil::grcTexturePC>::TContainer containers;
	if (it != hashes.end())
	{
		auto pos = std::distance(hashes.begin(), it);
		auto ptr = dict->values.data.Get() + pos;
		ptr->Set(&texture);
	}
	else
	{
		containers = dict->Insert(hash, &texture);
	}

	RageUtil::RSC5::BlockList blockList;
	blockList.AppendVirtual(dict, sizeof(*dict), nullptr);
	dict->DumpToMemory(blockList);

	hFile.reset();
	hFile.reset(CreateFileW(out.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	THROW_LAST_ERROR_IF(!hFile);

	RageUtil::RSC5::DumpToFile(hFile.get(), header, blockList);

	RageUtil::s_virtual = {};
	RageUtil::s_physical = {};
	RageUtil::s_ptrTable.clear();
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, [[maybe_unused]] LPARAM lParam)
{
	static HWND s_hPreview = nullptr;
	switch (message)
	{
	case WM_INITDIALOG:
	{
		CheckRadioButton(hDlg, IDC_QUOTE_CN, IDC_QUOTE_EN, IDC_QUOTE_CN);
		CheckRadioButton(hDlg, IDC_DWRITE, IDC_GDIP, IDC_DWRITE);

		CheckDlgButton(hDlg, IDC_GAME_IV, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_GAME_TLAD, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_GAME_TBOGT, BST_CHECKED);

		// Set WS_BORDER at runtime to get a 1 pixel border instead of 3d broder
		s_hPreview = GetDlgItem(hDlg, IDC_PREVIEW);
		auto style = GetWindowLongW(s_hPreview, GWL_STYLE);
		SetWindowLongW(s_hPreview, GWL_STYLE, style | WS_BORDER);
		SetWindowPos(s_hPreview, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

		SetDlgItemTextW(hDlg, IDC_PREVIEW_TEXT, L"测试文本");

		return static_cast<INT_PTR>(TRUE);
	}
	case WM_COMMAND:
		switch (auto wmId = LOWORD(wParam))
		{
		case IDCANCEL:
			EndDialog(hDlg, 0);
			return static_cast<INT_PTR>(TRUE);
		case IDC_SELECT_FONT:
		case IDC_SELECT_SYMBOL_FONT:
		{
			auto font = ChooseFontDialog(hDlg, -58);
			if (font.has_value())
			{
				font->lfHeight = -58;
				font->lfQuality = DEFAULT_QUALITY;
				switch (wmId)
				{
				case IDC_SELECT_FONT:
					g_font = *font;
					SetDlgItemTextW(hDlg, IDC_FONT, font->lfFaceName);
					if (*g_symbolFont.lfFaceName)
						break;
					[[fallthrough]];
				case IDC_SELECT_SYMBOL_FONT:
					g_symbolFont = *font;
					SetDlgItemTextW(hDlg, IDC_SYMBOL_FONT, font->lfFaceName);
					break;
				}
			}
		}
		break;
		case IDC_SELECT_DIR:
		{
			std::wstring buf(512, L'\0');
			do
			{
				DWORD size = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
				if (LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_LOCAL_MACHINE, LR"(Software\Rockstar Games\Grand Theft Auto IV)", L"InstallFolder", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf.data(), &size)) == ERROR_SUCCESS)
				{
					buf.resize(size / 2 - 1);
					break;
				}

				size = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
				if (LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_LOCAL_MACHINE, LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 12210)", L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf.data(), &size)) == ERROR_SUCCESS)
				{
					buf.resize(size / 2 - 1);
					break;
				}

				buf.clear();
			} while (0);

			if (!buf.empty())
			{
				fs::path path(std::move(buf));
				if (CheckAndFixGTAIVPath(path))
				{
					if (CheckCHSFiles(path))
					{
						g_gamePath = std::move(path);
						SetDlgItemTextW(hDlg, IDC_GAMEDIR, g_gamePath.c_str());
					}
					else
						TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"找到游戏目录，但缺少 char_table.dat。\n请确认已安装汉化补丁。", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
				}
				else
					TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"找到游戏目录，但缺少 fonts.wtd。\n请确认已正确安装游戏，可尝试验证完整性。", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
			}
			else
				TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"无法找到游戏目录，请手动选择", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
		}
		break;
		case IDM_SELECT_DIR:
		{
			try
			{
				auto fileDlg = wil::CoCreateInstance<IFileDialog>(CLSID_FileOpenDialog);

				FILEOPENDIALOGOPTIONS opts;
				THROW_IF_FAILED(fileDlg->GetOptions(&opts));
				THROW_IF_FAILED(fileDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));

				THROW_IF_FAILED(fileDlg->Show(hDlg));

				wil::com_ptr<IShellItem> result;
				THROW_IF_FAILED(fileDlg->GetResult(&result));

				wil::unique_cotaskmem_string pathStr;
				THROW_IF_FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &pathStr));

				fs::path path(pathStr.get());
				if (CheckAndFixGTAIVPath(path))
				{
					if (CheckCHSFiles(path))
					{
						g_gamePath = std::move(path);
						SetDlgItemTextW(hDlg, IDC_GAMEDIR, g_gamePath.c_str());
					}
					else
						TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"选择的目录下未找到 char_table.dat。\n请确认已安装汉化补丁。", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
				}
				else
					TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"选择的目录下未找到 fonts.wtd。", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
			}
			CATCH_LOG();
		}
		break;
		case IDM_OPEN_DIR:
			ShellExecuteW(hDlg, nullptr, g_gamePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			break;
		case IDC_GENERATE_PREVIEW:
			if (CheckFontSelected(hDlg))
			{
				try
				{
					UpdatePreview(s_hPreview, GetWindowString(GetDlgItem(hDlg, IDC_PREVIEW_TEXT)), IsDlgButtonChecked(hDlg, IDC_GDIP) == BST_CHECKED, IsDlgButtonChecked(hDlg, IDC_QUOTE_EN) == BST_CHECKED);
				}
				catch (...)
				{
					LOG_CAUGHT_EXCEPTION();
					TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"生成预览时出现错误", TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
				}
			}
			break;
		case IDC_GENERATE:
		{
			if (CheckFontSelected(hDlg) && CheckGamePathSelected(hDlg) && CheckCharTableDat(hDlg))
			{
				try
				{
					bool IV = IsDlgButtonChecked(hDlg, IDC_GAME_IV) == BST_CHECKED;
					bool TLAD = IsDlgButtonChecked(hDlg, IDC_GAME_TLAD) == BST_CHECKED;
					bool TBOGT = IsDlgButtonChecked(hDlg, IDC_GAME_TBOGT) == BST_CHECKED;

					if (!(IV || TLAD || TBOGT))
						break;

					std::wstring chars;
					{
						wil::unique_hfile hCharsFile(CreateFileW((g_gamePath / CharTableDatPath).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
						chars = ReadCharTableDatToUtf16String(hCharsFile.get());
					}

					bool useGDIP = IsDlgButtonChecked(hDlg, IDC_GDIP) == BST_CHECKED;
					bool replaceChars = IsDlgButtonChecked(hDlg, IDC_QUOTE_EN) == BST_CHECKED;

					auto dxt5Img = GenerateCharsImage(hDlg, chars, useGDIP, replaceChars);

					if (IV)
					{
						auto path = g_gamePath / NewFontsPathIV;
						fs::create_directories(path.parent_path());
						CreateWTD(g_gamePath / FontsPathIV, path, dxt5Img);
					}

					if (TBOGT)
					{
						auto path = g_gamePath / NewFontsPathTBoGT;
						fs::create_directories(path.parent_path());
						CreateWTD(g_gamePath / FontsPathTBoGT, path, dxt5Img);
					}

					if (TLAD)
					{
						auto path = g_gamePath / NewFontsPathTLAD;
						fs::create_directories(path.parent_path());
						CreateWTD(g_gamePath / FontsPathTLAD, path, dxt5Img);
					}

					TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"生成成功", TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
				}
				catch (...)
				{
					LOG_CAUGHT_EXCEPTION();
					TaskDialog(hDlg, nullptr, L"CWTDGen", nullptr, L"生成贴图时出现错误。\n如果已经替换汉化贴图，请尝试还原原版贴图。", TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
				}
			}
		}
		break;
		case IDC_FONT:
		case IDC_SYMBOL_FONT:
		case IDC_GAMEDIR:
			if (HIWORD(wParam) == EN_SETFOCUS)
				HideCaret(reinterpret_cast<HWND>(lParam));
			break;
		}
		break;
	case WM_NOTIFY:
		switch (reinterpret_cast<LPNMHDR>(lParam)->code)
		{
		case BCN_DROPDOWN:
		{
			auto dropDown = reinterpret_cast<LPNMBCDROPDOWN>(lParam);
			POINT pt = { dropDown->rcButton.left, dropDown->rcButton.bottom };
			ClientToScreen(dropDown->hdr.hwndFrom, &pt);

			wil::unique_hmenu hMenu(CreatePopupMenu());
			AppendMenuW(hMenu.get(), 0, IDM_SELECT_DIR, L"手动选择...");
			AppendMenuW(hMenu.get(), g_gamePath.empty() ? MF_DISABLED | MF_GRAYED : 0, IDM_OPEN_DIR, L"打开选择的文件夹");
			TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hDlg, nullptr);
		}
		break;
		}
		break;
	}
	return static_cast<INT_PTR>(FALSE);
}
