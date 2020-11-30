// This file contains code from
// https://github.com/ahmed605/SparkIV and
// https://github.com/citizenfx/fivem
// See https://github.com/ahmed605/SparkIV/blob/master/LICENSE and
// https://github.com/citizenfx/fivem/blob/master/code/LICENSE
// for license information.

#pragma once

namespace RageUtil
{
	namespace RSC5
	{
		enum struct ResourceType : uint32_t
		{
			TextureXBOX = 0x7, // xtd
			ModelXBOX = 0x6D, // xdr
			Generic = 0x01, // xhm / xad (Generic files as rsc?)
			Bounds = 0x20, // xbd, wbd
			Particles = 0x24, // xpfl
			Particles2 = 0x1B, // xpfl

			Texture = 0x8, // wtd
			Model = 0x6E, // wdr
			ModelFrag = 0x70, //wft
		};

		struct Header
		{
			static constexpr uint32_t MagicValue = 0x05435352; // RSC\x05

			uint32_t magic;
			ResourceType type;
			uint32_t flags;

			uint32_t GetVirtualSize() const
			{
				return (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8);
			}

			uint32_t GetPhysicalSize() const
			{
				return ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8);
			}

			static std::pair<uint32_t, uint32_t> CalculateFlag(uint32_t size)
			{
				constexpr uint32_t maxBase = 0x3F;

				uint32_t base = size >> 8;
				uint32_t shift = 0;

				while (base > maxBase)
				{
					if (base & 1)
					{
						base += 2;
					}
					base >>= 1;
					shift++;
				}

				// Pad non-even sizes
				if (base & 1)
				{
					base++;
				}

				uint32_t roundUpSize = base << (shift + 8);
				return { base | (shift << 11), roundUpSize };
			}

			static constexpr uint32_t MinVirtualSize = 4096;

			auto SetFlagSizes(uint32_t virtualSize, uint32_t physicalSize)
			{
				const auto vSizes = CalculateFlag(std::max(virtualSize, MinVirtualSize));
				const auto pSizes = CalculateFlag(physicalSize);
				flags = (flags & 0xC0000000) | vSizes.first | (pSizes.first << 15);
				return std::make_pair(vSizes.second, pSizes.second);
			}
		};
		static_assert(sizeof(Header) == 12);

		// For use to store our block data
		struct BlockList
		{
			struct BlockInfo
			{
				void* data;
				uint32_t size;
				uint32_t offset;
			};

			uint32_t virtualSize = 0;
			uint32_t physicalSize = 0;

			std::vector<BlockInfo> virtualBlocks;
			std::vector<BlockInfo> physicalBlocks;

			uint32_t AppendVirtual(void* data, uint32_t size)
			{
				const uint32_t offset = virtualSize;
				virtualBlocks.emplace_back(data, size, offset);
				virtualSize += RoundUp<16>(size);
				return offset;
			}

			uint32_t AppendPhysical(void* data, uint32_t size)
			{
				const uint32_t offset = physicalSize;
				physicalBlocks.emplace_back(data, size, offset);
				physicalSize += RoundUp<16>(size);
				return offset;
			}
		};

		constexpr size_t ChunkSize = 65536;

		auto ReadFromFile(HANDLE hFile)
		{
			Header header;
			ReadFileCheckSize(hFile, &header, sizeof(header));

			THROW_HR_IF(E_INVALIDARG, header.magic != Header::MagicValue);
			THROW_HR_IF(E_INVALIDARG, header.type != ResourceType::Texture);

			uint32_t decodedSize = header.GetVirtualSize() + header.GetPhysicalSize();
			auto decoded = std::make_unique<uint8_t[]>(decodedSize);

			unique_z_stream_inflate strm;
			int ret = inflateInit(&strm);
			THROW_HR_IF(E_FAIL, ret != Z_OK);

			strm.avail_out = decodedSize;
			strm.next_out = decoded.get();

			uint8_t buf[ChunkSize];
			do
			{
				DWORD read = 0;
				THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile, buf, ChunkSize, &read, nullptr));
				THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_HANDLE_EOF), read == 0);

				strm.avail_in = read;
				strm.next_in = buf;
				ret = inflate(&strm, Z_NO_FLUSH);
			} while (ret != Z_STREAM_END);

			return std::make_pair(header, std::move(decoded));
		}

		constexpr uint8_t PadByte = 0xcd;

		auto DumpToFile(HANDLE hFile, Header& header, const BlockList& blockList)
		{
			const auto [realVirtualSize, realPhysicalSize] = header.SetFlagSizes(blockList.virtualSize, blockList.physicalSize);
			WriteFileCheckSize(hFile, &header, sizeof(header));

			unique_z_stream_deflate strm;
			int ret = deflateInit(&strm, Z_BEST_COMPRESSION);
			THROW_HR_IF(E_FAIL, ret != Z_OK);

			auto DeflateWrite = [hFile, &strm](void* data, size_t size, bool flush = false) {
				strm.avail_in = static_cast<uInt>(size);
				strm.next_in = reinterpret_cast<Bytef*>(data);
				uint8_t buf[ChunkSize];
				do {
					strm.avail_out = ChunkSize;
					strm.next_out = buf;
					int ret = deflate(&strm, flush ? Z_FINISH : Z_NO_FLUSH);
					THROW_HR_IF(E_FAIL, ret == Z_STREAM_ERROR);
					WriteFileCheckSize(hFile, buf, ChunkSize - strm.avail_out);
				} while (strm.avail_out == 0);
			};
			auto WritePadBytes = [&DeflateWrite](size_t size) {
				if (size == 0) return;
				uint8_t buf[Header::MinVirtualSize];
				std::fill_n(buf, std::min(size, std::size(buf)), PadByte);
				DeflateWrite(buf, size);
			};

			for (auto& b : blockList.virtualBlocks)
			{
				DeflateWrite(b.data, b.size);
				auto padSize = RoundUp<16>(b.size) - b.size;
				WritePadBytes(padSize);
			}

			WritePadBytes(realVirtualSize - blockList.virtualSize);

			for (auto& b : blockList.physicalBlocks)
			{
				DeflateWrite(b.data, b.size);
				auto padSize = RoundUp<16>(b.size) - b.size;
				WritePadBytes(padSize);
			}

			DeflateWrite(nullptr, 0, true);
		}
	}

	enum struct pgPtrBlockType : uint32_t
	{
		Virtual = 5,
		Physical = 6,
		Memory = 0xf // Hack
	};

	static std::span<uint8_t> s_virtual;
	static std::span<uint8_t> s_physical;
	static std::vector<void*> s_ptrTable;

	template<typename T, pgPtrBlockType DefaultBlockType = pgPtrBlockType::Virtual>
	struct pgPtr
	{
		uint32_t offset : 28;
		pgPtrBlockType blockType : 4;

		[[nodiscard]] T* Get() const
		{
			switch (blockType)
			{
			case pgPtrBlockType::Virtual:
				THROW_HR_IF(E_INVALIDARG, !CheckType() || (offset + sizeof(T)) > s_virtual.size());
				return reinterpret_cast<T*>(s_virtual.data() + offset);
			case pgPtrBlockType::Physical:
				THROW_HR_IF(E_INVALIDARG, !CheckType() || (offset + sizeof(T)) > s_physical.size());
				return reinterpret_cast<T*>(s_physical.data() + offset);
			case pgPtrBlockType::Memory:
				return reinterpret_cast<T*>(s_ptrTable[static_cast<size_t>(offset)]);
			}
			THROW_HR(E_INVALIDARG); // Unknown blockType
		}

		[[nodiscard]] T* operator->() const
		{
			return Get();
		}

		void Set(const T* ptr)
		{
			auto voidPtr = reinterpret_cast<void*>(const_cast<T*>(ptr));
			if (blockType == pgPtrBlockType::Memory)
			{
				s_ptrTable[static_cast<size_t>(offset)] = voidPtr;
			}
			else
			{
				auto index = s_ptrTable.size();
				s_ptrTable.emplace_back(voidPtr);

				offset = static_cast<uint32_t>(index);
				blockType = pgPtrBlockType::Memory;
			}
		}

		void SetOffset(uint32_t off, pgPtrBlockType type = DefaultBlockType)
		{
			offset = off;
			blockType = type;
		}

		bool CheckType() const { return blockType == DefaultBlockType; }
	};
	static_assert(sizeof(pgPtr<int>) == 4);

	template<typename T>
	struct pgArray
	{
		pgPtr<T> data;
		uint16_t size;
		uint16_t capacity;

		[[nodiscard]] auto InsertSorted(T value)
		{
			auto ptr = data.Get();
			std::vector<T> container;
			container.reserve(static_cast<size_t>(size) + 1);
			container.assign(ptr, ptr + size);

			// Assume the data is sorted
			auto it = std::lower_bound(container.cbegin(), container.cend(), value);
			size_t pos = it - container.cbegin();
			container.insert(it, value);

			data.Set(container.data());
			++size;
			capacity = size;

			return std::make_pair(std::move(container), pos);
		}

		[[nodiscard]] auto InsertAt(size_t pos, T value)
		{
			auto ptr = data.Get();
			std::vector<T> container;
			container.reserve(static_cast<size_t>(size) + 1);
			container.assign(ptr, ptr + size);
			container.insert(container.cbegin() + pos, value);

			data.Set(container.data());
			++size;
			capacity = size;

			return container;
		}
	};
	static_assert(sizeof(pgArray<int>) == 8);

	template<typename T>
	using pgObjectArray = pgArray<pgPtr<T>>;
	static_assert(sizeof(pgObjectArray<int>) == 8);

	struct datBase
	{
		uint32_t vtable;
	};
	static_assert(sizeof(datBase) == 4);

	// Repersent file format
	struct BlockMap
	{
		uint16_t virtualCount; // 0
		uint16_t physicalCount; // 0

		struct pgBlockInfo
		{
			uint32_t offset;
			uint32_t data;
			uint32_t size;
		} blocks[43]; // 0xCD

		uint32_t baseAllocationSize[2]; // 0xCD
	};
	static_assert(sizeof(BlockMap) == 528);

	struct pgBase : datBase
	{
		pgPtr<BlockMap> blockMap;
	};
	static_assert(sizeof(pgBase) == 8);

	template<typename T>
	struct pgDictionary : public pgBase
	{
		pgPtr<pgBase> parent;
		uint32_t usageCount;
		pgArray<uint32_t> hashes;
		pgObjectArray<T> values;

		[[nodiscard]] auto Insert(uint32_t hash, T* value)
		{
			auto [hashContainer, pos] = hashes.InsertSorted(hash);
			pgPtr<T> ptr;
			ptr.Set(value);
			auto valueContainer = values.InsertAt(pos, ptr);
			return std::make_pair(std::move(hashContainer), std::move(valueContainer));
		}

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			blockList.AppendVirtual(this, sizeof(pgDictionary));
			
			auto offset = blockList.AppendVirtual(blockMap.Get(), sizeof(BlockMap));
			blockMap.SetOffset(offset);

			const auto size = static_cast<size_t>(values.size);
			const auto objects = std::make_unique<T*[]>(size);

			const auto objsPtr = values.data.Get();
			for (size_t i = 0; i < size; ++i)
			{
				const auto object = objsPtr[i].Get();
				offset = blockList.AppendVirtual(object, sizeof(T));
				objsPtr[i].SetOffset(offset);
				objects.get()[i] = object;
			}

			for (size_t i = 0; i < size; ++i)
			{
				objects.get()[i]->DumpToMemory(blockList);
			}

			assert(size == hashes.size);
			offset = blockList.AppendVirtual(hashes.data.Get(), static_cast<uint32_t>(sizeof(uint32_t) * size));
			hashes.data.SetOffset(offset);

			offset = blockList.AppendVirtual(objsPtr, static_cast<uint32_t>(sizeof(pgPtr<T>) * size));
			values.data.SetOffset(offset);
		}
	};
	static_assert(sizeof(pgDictionary<int>) == 32);

	struct grcTexture : pgBase
	{
		uint8_t objectType;
		uint8_t depth;
		uint16_t usageCount;
		uint32_t pad;
		uint32_t pad2;
		pgPtr<char> name;
		uint32_t nativeHandle;
		uint16_t width;
		uint16_t height;
		D3DFORMAT pixelFormat;
		uint16_t stride;
		uint8_t textureType;
		uint8_t levels;
	};

	struct grcTexturePC : grcTexture
	{
		float unk28[3];
		float unk34[3];
		uint32_t next;
		uint32_t prev;
		pgPtr<uint8_t, pgPtrBlockType::Physical> pixelData; // In physical data segment
		uint8_t pad[4];

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			const auto namePtr = name.Get();
			auto offset = blockList.AppendVirtual(namePtr, static_cast<uint32_t>(strlen(namePtr) + 1));
			name.SetOffset(offset);

			DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
			switch (pixelFormat)
			{
			case D3DFMT_DXT1:
				fmt = DXGI_FORMAT_BC1_UNORM; break;
			case D3DFMT_DXT2:
			case D3DFMT_DXT3:
				fmt = DXGI_FORMAT_BC2_UNORM; break;
			case D3DFMT_DXT4:
			case D3DFMT_DXT5:
				fmt = DXGI_FORMAT_BC3_UNORM; break;
			case D3DFMT_A8R8G8B8:
				fmt = DXGI_FORMAT_B8G8R8A8_UNORM; break;
			}
			THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_PIXEL_FORMAT), fmt == DXGI_FORMAT_UNKNOWN);

			size_t rowPitch, slicePitch;
			THROW_IF_FAILED(DirectX::ComputePitch(fmt, width, height, rowPitch, slicePitch));
			offset = blockList.AppendPhysical(pixelData.Get(), static_cast<uint32_t>(slicePitch));
			pixelData.SetOffset(offset);
		}
	};
	static_assert(sizeof(grcTexturePC) == 80);

	constexpr uint32_t HashString(const char* string, uint32_t hash = 0)
	{
		for (; *string; ++string)
		{
			hash += *string;
			hash += (hash << 10);
			hash ^= (hash >> 6);
		}

		hash += (hash << 3);
		hash ^= (hash >> 11);
		hash += (hash << 15);

		return hash;
	}
}
