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

void UpdatePreview(HWND hWnd, std::wstring_view text, bool useGDI)
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
		wil::unique_hbitmap hBitmap(CreateDIB(hdcWnd.get(), width, height, 32));
		THROW_HR_IF(E_FAIL, !hBitmap);
		auto selectBitmap = wil::SelectObject(hdc.get(), hBitmap.get());

		// Draw checkered background
		wil::unique_hbrush hBrush1(CreateSolidBrush(0x202020));
		THROW_HR_IF(E_FAIL, !hBrush1);
		wil::unique_hbrush hBrush2(CreateSolidBrush(0x303030));
		THROW_HR_IF(E_FAIL, !hBrush2);

		RECT fillRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
		FillRect(hdc.get(), &fillRect, hBrush1.get());
		for (uint32_t y = 0; y < yChars; ++y)
		{
			for (uint32_t x = y % 2; x < xChars; x += 2)
			{
				fillRect.left = static_cast<LONG>(x * CHAR_WIDTH);
				fillRect.top = static_cast<LONG>(y * CHAR_HEIGHT);
				fillRect.right = fillRect.left + CHAR_WIDTH;
				fillRect.bottom = fillRect.top + CHAR_HEIGHT;
				FillRect(hdc.get(), &fillRect, hBrush2.get());
			}
		}

		if (useGDI)
		{
			GDIDrawCharacters(hdc.get(), text, xChars, yChars);
		}
		else
		{
			wil::com_ptr<ID2D1DCRenderTarget> dcRenderTarget;
			D2D1_RENDER_TARGET_PROPERTIES props = {
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				{
					DXGI_FORMAT_B8G8R8A8_UNORM,
					D2D1_ALPHA_MODE_IGNORE
				},
				0,
				0,
				D2D1_RENDER_TARGET_USAGE_NONE,
				D2D1_FEATURE_LEVEL_DEFAULT
			};
			THROW_IF_FAILED(g_d2dFactory->CreateDCRenderTarget(&props, &dcRenderTarget));

			RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			THROW_IF_FAILED(dcRenderTarget->BindDC(hdc.get(), &rect));
			dcRenderTarget->BeginDraw();
			DWriteDrawCharacters(dcRenderTarget.get(), text, xChars, yChars);
			dcRenderTarget->EndDraw();
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

			wil::unique_hdc hdcScale(CreateCompatibleDC(hdcWnd.get()));
			THROW_HR_IF(E_FAIL, !hdcScale);
			wil::unique_hbitmap hBitmapScale(CreateCompatibleBitmap(hdcWnd.get(), scaledWidth, scaledHeight));
			THROW_HR_IF(E_FAIL, !hBitmapScale);
			auto selectScale = wil::SelectObject(hdcScale.get(), hBitmapScale.get());

			SetStretchBltMode(hdcScale.get(), HALFTONE);
			StretchBlt(hdcScale.get(), 0, 0, scaledWidth, scaledHeight, hdc.get(), 0, 0, width, height, SRCCOPY);

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
		CheckRadioButton(hDlg, IDC_DWRITE, IDC_GDI, IDC_DWRITE);

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
				bool isFont = wmId == IDC_SELECT_FONT;
				if (isFont)
					g_font = *font;
				else
					g_symbolFont = *font;
				SetDlgItemTextW(hDlg, isFont ? IDC_FONT : IDC_SYMBOL_FONT, font->lfFaceName);
			}
		}
		break;
		case IDC_SELECT_DIR:
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
		case IDC_GENERATE:
		{
			
		}
		break;
		}
		break;
	}
	return static_cast<INT_PTR>(FALSE);
}
