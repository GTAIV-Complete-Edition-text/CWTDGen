#pragma once

// https://docs.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/author-coclasses#add-helper-types-and-functions
// License: see the https://github.com/MicrosoftDocs/windows-uwp/blob/docs/LICENSE-CODE file
auto GetModuleFsPath(HMODULE hModule)
{
	std::wstring path(MAX_PATH, L'\0');
	DWORD actualSize;
	while (1)
	{
		actualSize = GetModuleFileNameW(hModule, path.data(), static_cast<DWORD>(path.size()));

		if (static_cast<size_t>(actualSize) + 1 > path.size())
			path.resize(path.size() * 2);
		else
			break;
	}
	path.resize(actualSize);
	return fs::path(path);
}

std::wstring GetWindowString(HWND hWnd)
{
	std::wstring str;
	int len = GetWindowTextLengthW(hWnd);
	if (len != 0)
	{
		str.resize(static_cast<size_t>(len) + 1);
		len = GetWindowTextW(hWnd, str.data(), len + 1);
		str.resize(static_cast<size_t>(len));
	}
	return str;
}
