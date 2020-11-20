#pragma once

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
