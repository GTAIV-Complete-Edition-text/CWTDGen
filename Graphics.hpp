#pragma once

inline HRESULT HRESULT_FROM_GPSTATUS(Gdiplus::Status status)
{
	HRESULT hr = E_FAIL;
	switch (status)
	{
	case Gdiplus::Ok:
		hr = S_OK;
		break;
	case Gdiplus::InvalidParameter:
		hr = E_INVALIDARG;
		break;
	case Gdiplus::OutOfMemory:
		hr = E_OUTOFMEMORY;
		break;
	case Gdiplus::InsufficientBuffer:
		hr = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
		break;
	case Gdiplus::Aborted:
		hr = E_ABORT;
		break;
	case Gdiplus::ObjectBusy:
		hr = E_PENDING;
		break;
	case Gdiplus::FileNotFound:
		hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		break;
	case Gdiplus::AccessDenied:
		hr = E_ACCESSDENIED;
		break;
	case Gdiplus::UnknownImageFormat:
		hr = HRESULT_FROM_WIN32(ERROR_INVALID_PIXEL_FORMAT);
		break;
	case Gdiplus::NotImplemented:
		hr = E_NOTIMPL;
		break;
	case Gdiplus::Win32Error:
		hr = HRESULT_FROM_WIN32(GetLastError());
		break;
	}
	return hr;
}

auto CreateDIB(HDC hdc, LONG width, LONG height, WORD bitCount, void** bitmapBits = nullptr)
{
	BITMAPINFO bmi = { .bmiHeader = {
		.biSize = sizeof(BITMAPINFOHEADER),
		.biWidth = width,
		.biHeight = -height, // negative is top-down bitmap
		.biPlanes = 1,
		.biBitCount = bitCount,
		.biCompression = BI_RGB,
	} };
	return wil::unique_hbitmap(CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, bitmapBits, nullptr, 0));
}

void GDIDrawCheckeredBackground(HDC hdc, LONG width, LONG height, uint32_t xChars, uint32_t yChars, COLORREF color1 = 0x202020, COLORREF color2 = 0x303030)
{
	wil::unique_hbrush hBrush1(CreateSolidBrush(color1));
	THROW_HR_IF(E_FAIL, !hBrush1);
	wil::unique_hbrush hBrush2(CreateSolidBrush(color2));
	THROW_HR_IF(E_FAIL, !hBrush2);

	RECT fillRect = { 0, 0, width, height };
	FillRect(hdc, &fillRect, hBrush1.get());
	for (uint32_t y = 0; y < yChars; ++y)
	{
		for (uint32_t x = y % 2; x < xChars; x += 2)
		{
			fillRect.left = static_cast<LONG>(x * CHAR_WIDTH);
			fillRect.top = static_cast<LONG>(y * CHAR_HEIGHT);
			fillRect.right = fillRect.left + CHAR_WIDTH;
			fillRect.bottom = fillRect.top + CHAR_HEIGHT;
			FillRect(hdc, &fillRect, hBrush2.get());
		}
	}
}

void SetBitmapAlpha(RGBQUAD* bitmapBits, uint32_t width, uint32_t height, BYTE alpha)
{
	for (uint32_t i = 0; i < width * height; ++i)
	{
		auto pixel = &bitmapBits[i];
		pixel->rgbReserved = alpha; // alpha
	}
}

wil::unique_hbitmap GDIScaleBitmap(HDC hdcRef, HDC hdcSrc, int width, int height, int scaledWidth, int scaledHeight, int stretchBltMode = HALFTONE)
{
	wil::unique_hdc hdcScale(CreateCompatibleDC(hdcRef));
	THROW_HR_IF(E_FAIL, !hdcScale);
	wil::unique_hbitmap hBitmapScale(CreateCompatibleBitmap(hdcRef, scaledWidth, scaledHeight));
	THROW_HR_IF(E_FAIL, !hBitmapScale);

	{
		auto selectScale = wil::SelectObject(hdcScale.get(), hBitmapScale.get());
		SetStretchBltMode(hdcScale.get(), stretchBltMode);
		StretchBlt(hdcScale.get(), 0, 0, scaledWidth, scaledHeight, hdcSrc, 0, 0, width, height, SRCCOPY);
	}

	return hBitmapScale;
}

void GDIDrawCharacters(HDC hdc, std::wstring_view text, uint32_t xChars, uint32_t yChars)
{
	wil::unique_hfont hFont(CreateFontIndirectW(&g_font));
	THROW_HR_IF(E_FAIL, !hFont);

	wil::unique_hfont hSymbolFont(CreateFontIndirectW(&g_symbolFont));
	THROW_HR_IF(E_FAIL, !hSymbolFont);

	auto select = wil::SelectObject(hdc, hFont.get());
	bool symbolFontSelected = false;

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, 0xffffff);

	size_t i = 0;
	for (uint32_t y = 0; y < yChars; ++y)
	{
		for (uint32_t x = 0; x < xChars && i < text.size(); ++x, ++i)
		{
			bool isSymbol = SYMBOL_SET.contains(text[i]);
			if (isSymbol != symbolFontSelected)
			{
				SelectObject(hdc, isSymbol ? hSymbolFont.get() : hFont.get());
				symbolFontSelected = isSymbol;
			}

			RECT rect;
			rect.left = static_cast<LONG>(x * CHAR_WIDTH);
			rect.top = static_cast<LONG>(y * CHAR_HEIGHT);
			rect.right = rect.left + CHAR_WIDTH;
			rect.bottom = rect.top + CHAR_HEIGHT;

			DrawTextW(hdc, &text[i], 1, &rect, DT_CENTER | DT_NOPREFIX | DT_SINGLELINE | DT_BOTTOM);
		}
	}
}

void GpDrawCharacters(HDC hdc, std::wstring_view text, uint32_t xChars, uint32_t yChars)
{
	Gp::Graphics graphics(hdc);
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(graphics.GetLastStatus()));
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(graphics.SetSmoothingMode(Gp::SmoothingModeHighQuality)));
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(graphics.SetTextRenderingHint(Gp::TextRenderingHintAntiAliasGridFit)));

	Gp::Font font(hdc, &g_font);
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(font.GetLastStatus()));
	Gp::Font symbolFont(hdc, &g_symbolFont);
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(symbolFont.GetLastStatus()));

	Gp::StringFormat format;
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(format.GetLastStatus()));
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(format.SetAlignment(Gp::StringAlignmentCenter)));
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(format.SetLineAlignment(Gp::StringAlignmentCenter)));

	Gp::SolidBrush brush(0xffffffff);
	THROW_IF_FAILED(HRESULT_FROM_GPSTATUS(brush.GetLastStatus()));

	size_t i = 0;
	for (uint32_t y = 0; y < yChars; ++y)
	{
		for (uint32_t x = 0; x < xChars && i < text.size(); ++x, ++i)
		{
			bool isSymbol = SYMBOL_SET.contains(text[i]);

			Gp::RectF rect(static_cast<Gp::REAL>(x * CHAR_WIDTH), static_cast<Gp::REAL>(y * CHAR_HEIGHT), CHAR_WIDTH, CHAR_HEIGHT);
			graphics.DrawString(&text[i], 1, isSymbol ? &symbolFont : &font, rect, &format, &brush);
		}
	}
}

void DWriteDrawCharacters(ID2D1RenderTarget* renderTarget, std::wstring_view text, uint32_t xChars, uint32_t yChars, float fontSize = 64.0f)
{
	wil::com_ptr<IDWriteTextFormat> textFormat;
	THROW_IF_FAILED(g_dwriteFactory->CreateTextFormat(g_font.lfFaceName, nullptr, static_cast<DWRITE_FONT_WEIGHT>(g_font.lfWeight), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &textFormat));
	THROW_IF_FAILED(textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	THROW_IF_FAILED(textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR));

	wil::com_ptr<IDWriteTextFormat> symbolTextFormat;
	THROW_IF_FAILED(g_dwriteFactory->CreateTextFormat(g_symbolFont.lfFaceName, nullptr, static_cast<DWRITE_FONT_WEIGHT>(g_symbolFont.lfWeight), DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &textFormat));
	THROW_IF_FAILED(textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
	THROW_IF_FAILED(textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR));

	wil::com_ptr<ID2D1SolidColorBrush> brush;
	THROW_IF_FAILED(renderTarget->CreateSolidColorBrush(D2D1::ColorF(0xffffff), &brush));

	size_t i = 0;
	for (uint32_t y = 0; y < yChars; ++y)
	{
		for (uint32_t x = 0; x < xChars && i < text.size(); ++x, ++i)
		{
			bool isSymbol = SYMBOL_SET.contains(text[i]);

			D2D1_RECT_F rect;
			rect.left = static_cast<float>(x * CHAR_WIDTH);
			rect.top = static_cast<float>(y * CHAR_HEIGHT);
			rect.right = rect.left + CHAR_WIDTH;
			rect.bottom = rect.top + CHAR_HEIGHT;

			renderTarget->DrawText(&text[i], 1, isSymbol ? symbolTextFormat.get() : textFormat.get(), rect, brush.get());
		}
	}
}

void DWriteDrawCharacters(HDC hdc, LONG width, LONG height, std::wstring_view text, uint32_t xChars, uint32_t yChars, float fontSize = 64.0f)
{
	wil::com_ptr<ID2D1DCRenderTarget> dcRenderTarget;
	D2D1_RENDER_TARGET_PROPERTIES props = {
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		{
			DXGI_FORMAT_B8G8R8A8_UNORM,
			D2D1_ALPHA_MODE_PREMULTIPLIED
		},
		0, 0,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	};
	THROW_IF_FAILED(g_d2dFactory->CreateDCRenderTarget(&props, &dcRenderTarget));

	RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	THROW_IF_FAILED(dcRenderTarget->BindDC(hdc, &rect));

	dcRenderTarget->BeginDraw();
	DWriteDrawCharacters(dcRenderTarget.get(), text, xChars, yChars, fontSize);
	dcRenderTarget->EndDraw();
}
