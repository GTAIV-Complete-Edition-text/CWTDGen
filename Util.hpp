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

inline void ReadFileCheckSize(HANDLE hFile, void* buffer, DWORD size)
{
	DWORD read;
	THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile, buffer, size, &read, nullptr));
	THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), size != read);
}

inline void WriteFileCheckSize(HANDLE hFile, void* data, DWORD size)
{
	DWORD written;
	THROW_IF_WIN32_BOOL_FALSE(WriteFile(hFile, data, size, &written, nullptr));
	THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_HANDLE_DISK_FULL), size != written);
}

template <uint32_t multiple>
inline uint32_t RoundUp(uint32_t i)
{
	static_assert(multiple && ((multiple & (multiple - 1)) == 0));
	return (i + (multiple - 1)) & ~(multiple - 1);
}

// https://msdn.microsoft.com/en-us/magazine/mt763237
std::wstring Utf8ToUtf16(std::string_view utf8)
{
	if (utf8.empty())
	{
		return {};
	}

	constexpr DWORD kFlags = MB_ERR_INVALID_CHARS;
	const int utf8Length = static_cast<int>(utf8.length());
	const int utf16Length = MultiByteToWideChar(
		CP_UTF8,
		kFlags,
		utf8.data(),
		utf8Length,
		nullptr,
		0
	);
	THROW_LAST_ERROR_IF(utf16Length == 0);

	std::wstring utf16(utf16Length, L'\0');
	const int result = MultiByteToWideChar(
		CP_UTF8,
		kFlags,
		utf8.data(),
		utf8Length,
		utf16.data(),
		utf16Length
	);
	THROW_LAST_ERROR_IF(result == 0);

	return utf16;
}

std::wstring ReadTextToUtf16String(HANDLE hFile)
{
	constexpr size_t ChunkSize = 16384;
	std::vector<uint8_t> data;
	DWORD read = 0;
	do
	{
		auto offset = data.size();
		data.resize(offset + ChunkSize);

		read = 0;
		THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile, data.data() + offset, ChunkSize, &read, nullptr));
		data.resize(offset + static_cast<size_t>(read));
	} while (read != 0);
	THROW_HR_IF(E_INVALIDARG, data.size() < 4);
	if (data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) // UTF-8 BOM
		return Utf8ToUtf16(std::string_view(reinterpret_cast<const char*>(data.data() + 3), data.size() - 3));
	else if (data[0] == 0xff && data[1] == 0xFE) // UTF-16LE BOM
		return std::wstring(reinterpret_cast<const wchar_t*>(data.data() + 2), data.size() / 2 - 1);
	return Utf8ToUtf16(std::string_view(reinterpret_cast<const char*>(data.data()), data.size())); // Treat data as UTF-8
}

std::wstring ReadCharTableDatToUtf16String(HANDLE hFile)
{
	DWORD size = GetFileSize(hFile, nullptr);
	THROW_LAST_ERROR_IF(size == INVALID_FILE_SIZE);

	uint32_t count = 0;
	ReadFileCheckSize(hFile, &count, sizeof(count));

	const DWORD strSize = count * sizeof(uint32_t);
	THROW_HR_IF(E_INVALIDARG, sizeof(count) + strSize != size);

	auto u32str = std::make_unique<uint32_t[]>(count);
	ReadFileCheckSize(hFile, u32str.get(), strSize);

	std::wstring result;
	result.reserve(count);

	for (size_t i = 0; i < count; ++i)
	{
		const auto u32char = u32str[i];
		THROW_HR_IF(E_NOTIMPL, u32char > UINT16_MAX);
		result.push_back(static_cast<wchar_t>(u32char));
	}

	return result;
}

uint32_t Log2(uint32_t x)
{
	unsigned long index;
	if (!_BitScanReverse(&index, x)) return UINT32_MAX;
	return index;
}

uint32_t Log2Ceil(uint32_t x)
{
	return Log2(x - 1) + 1;
}

template<size_t N>
constexpr bool IsWCharInRanges(const std::pair<wchar_t, wchar_t> (&ranges)[N], wchar_t ch)
{
	for (const auto& range : ranges)
	{
		if (range.first <= ch && ch <= range.second)
			return true;
	}
	return false;
}
