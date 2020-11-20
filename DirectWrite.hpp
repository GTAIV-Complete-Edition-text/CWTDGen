#pragma once

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
