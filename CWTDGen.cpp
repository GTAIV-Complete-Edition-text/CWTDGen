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
	static const fs::path list[] = {
		LR"(pc\textures\fonts.wtd)",
		LR"(TBoGT\pc\textures\fonts.wtd)",
		LR"(TLAD\pc\textures\fonts.wtd)"
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

void UpdatePreview(HWND hWnd, std::wstring_view text, bool useGDIP)
{
	if (text.empty())
		return;

	RECT rc;
	THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hWnd, &rc));

	const uint32_t wndWidth = rc.right - rc.left, wndHeight = rc.bottom - rc.top;
	uint32_t xMaxChars = wndWidth / CHAR_WIDTH, yMaxChars = wndHeight / CHAR_HEIGHT;
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

	const uint32_t width = xChars * CHAR_WIDTH, height = yChars * CHAR_HEIGHT;

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
			GpDrawCharacters(hdc.get(), text, xChars, yChars);
		}
		else
		{
			DWriteDrawCharacters(hdc.get(), width, height, text, xChars, yChars);
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
			auto font = ChooseFontDialog(hDlg, 64);
			if (font.has_value())
			{
				font->lfHeight = -64;
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
				if (LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_LOCAL_MACHINE, LR"(Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 12210)", L"InstallLocation", RRF_RT_REG_SZ, nullptr, buf.data(), &size)) == ERROR_SUCCESS)
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
					g_gamePath = std::move(path);
					SetDlgItemTextW(hDlg, IDC_GAMEDIR, g_gamePath.c_str());
				}
			}
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
					g_gamePath = std::move(path);
					SetDlgItemTextW(hDlg, IDC_GAMEDIR, g_gamePath.c_str());
				}
			}
			CATCH_LOG();
		}
		break;
		case IDC_GENERATE_PREVIEW:
			UpdatePreview(s_hPreview, GetWindowString(GetDlgItem(hDlg, IDC_PREVIEW_TEXT)), IsDlgButtonChecked(hDlg, IDC_GDIP));
			break;
		case IDC_GENERATE:
		{
			wil::unique_hfile hFile(CreateFileW(L"fonts.wtd", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));

			auto [header, data] = RageUtil::RSC5::ReadFromFile(hFile.get());
			RageUtil::s_virtual = { data.get(), header.GetVirtualSize() };
			RageUtil::s_physical = { data.get() + RageUtil::s_virtual.size(), header.GetPhysicalSize() };

			auto dict = reinterpret_cast<RageUtil::pgDictionary<RageUtil::grcTexturePC>*>(data.get());

			auto texture = *dict->values.data.Get()->Get(); // copy
			texture.name.Set("pack:/fontx.dds");
			texture.next = 0;
			texture.prev = 0;

			auto containers = dict->Insert(RageUtil::HashString("fontx"), &texture);

			RageUtil::RSC5::BlockList bm;
			dict->DumpToMemory(bm);

			RageUtil::s_virtual = {};
			RageUtil::s_physical = {};
			RageUtil::s_ptrTable.clear();

			hFile.reset(CreateFileW(L"fonts1.wtd", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
			RageUtil::RSC5::DumpToFile(hFile.get(), header, bm);
		}
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
			TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hDlg, nullptr);
		}
		break;
		}
		break;
	}
	return static_cast<INT_PTR>(FALSE);
}
