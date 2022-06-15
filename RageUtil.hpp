// This file contains code from
// https://github.com/ahmed605/SparkIV and
// https://github.com/citizenfx/fivem
// See https://github.com/ahmed605/SparkIV/blob/master/LICENSE and
// https://github.com/citizenfx/fivem/blob/master/code/LICENSE
// for license information.

#pragma once

namespace RageUtil
{
	static std::span<uint8_t> s_virtual;
	static std::span<uint8_t> s_physical;
	static std::vector<void*> s_ptrTable;

	enum struct pgPtrBlockType : uint32_t
	{
		Virtual = 5,
		Physical = 6,
		Memory = 0xf // Hack
	};

	template<pgPtrBlockType DefaultBlockType>
	struct pgPtr
	{
		uint32_t offset : 28;
		pgPtrBlockType blockType : 4;

		void SetOffset(uint32_t off, pgPtrBlockType type = DefaultBlockType)
		{
			offset = off;
			blockType = type;
		}

		bool CheckType() const { return blockType == DefaultBlockType; }
	};

	template<typename T, pgPtrBlockType DefaultBlockType = pgPtrBlockType::Virtual>
	struct pgPtrT : pgPtr<DefaultBlockType>
	{
		using pgPtr<DefaultBlockType>::offset;
		using pgPtr<DefaultBlockType>::blockType;
		using pgPtr<DefaultBlockType>::CheckType;

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
	};
	static_assert(sizeof(pgPtrT<int>) == 4);

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

		struct RSC5Flags
		{
#define DEF_BLOCK_FLAG(t) \
	uint32_t t##Block0Count : 1; \
	uint32_t t##Block1Count : 1; \
	uint32_t t##Block2Count : 1; \
	uint32_t t##Block3Count : 1; \
	uint32_t t##Block4Count : 7; \
	uint32_t t##BlockSize : 4;

			DEF_BLOCK_FLAG(v); // virtual
			DEF_BLOCK_FLAG(p); // physical

#undef DEF_BLOCK_FLAG

			uint32_t unk0 : 1;
			uint32_t unk1 : 1;
		};
		static_assert(sizeof(RSC5Flags) == 4);

		union RSC5FlagsUint32
		{
			RSC5Flags flags;
			uint32_t uint32;
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
		};
		static_assert(sizeof(Header) == 12);

		// For use to store our block data
		struct BlockList
		{
			template<pgPtrBlockType BlockType>
			struct BlockInfo
			{
				void* data;
				uint32_t size;
				pgPtr<BlockType>* offsetPos;
			};

			using VBlockInfo = BlockInfo<pgPtrBlockType::Virtual>;
			using PBlockInfo = BlockInfo<pgPtrBlockType::Physical>;

			std::vector<VBlockInfo> virtualBlocks;
			std::vector<PBlockInfo> physicalBlocks;

			void AppendVirtual(void* data, uint32_t size, pgPtr<pgPtrBlockType::Virtual>* offsetPos)
			{
				virtualBlocks.emplace_back(data, size, offsetPos);
			}

			template<typename T>
			void AppendVirtualPtr(pgPtrT<T, pgPtrBlockType::Virtual>& ptr, uint32_t size = sizeof(T))
			{
				AppendVirtual(ptr.Get(), size, &ptr);
			}

			void AppendPhysical(void* data, uint32_t size, pgPtr<pgPtrBlockType::Physical>* offsetPos)
			{
				physicalBlocks.emplace_back(data, size, offsetPos);
			}

			template<typename T>
			void AppendPhysicalPtr(pgPtrT<T, pgPtrBlockType::Physical>& ptr, uint32_t size = sizeof(T))
			{
				AppendPhysical(ptr.Get(), size, &ptr);
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

		uint32_t SortAndCalculateFlags(BlockList& blockList)
		{
			RSC5FlagsUint32 f;
			f.uint32 = 0;

			std::sort(blockList.virtualBlocks.begin() + 1, blockList.virtualBlocks.end(), [](const BlockList::VBlockInfo& l, const BlockList::VBlockInfo& r) {
				return l.size > r.size;
			});

			uint32_t virtualSize = 0;
			for (auto& b : blockList.virtualBlocks)
			{
				if (b.offsetPos)
					b.offsetPos->SetOffset(virtualSize);
				virtualSize += RoundUp<16>(b.size);
			}

			// Assume virtual size <= 4096 and allocate a single level 4 block
			THROW_HR_IF(E_NOTIMPL, virtualSize > 4096);
			f.flags.vBlock4Count = 1;

			std::sort(blockList.physicalBlocks.begin(), blockList.physicalBlocks.end(), [](const BlockList::PBlockInfo& l, const BlockList::PBlockInfo& r) {
				return l.size > r.size;
			});

			uint32_t physicalSize = 0;
			for (auto& b : blockList.physicalBlocks)
			{
				b.offsetPos->SetOffset(physicalSize);
				physicalSize += RoundUp<16>(b.size);
			}

			const uint32_t biggestBlockSize = blockList.physicalBlocks.front().size;
			uint32_t phyBlockSize = Log2Ceil(biggestBlockSize);
			if (phyBlockSize < 12)
				phyBlockSize = 8;
			else
				phyBlockSize -= 4;

			uint32_t currentPhyBlockRemain = (1 << 4 << phyBlockSize) - biggestBlockSize;
			uint8_t levelsCount[5] = { 0, 0, 0, 0, 1 };
			for (auto it = blockList.physicalBlocks.begin() + 1; it != blockList.physicalBlocks.end(); ++it)
			{
				if (currentPhyBlockRemain >= it->size)
				{
					currentPhyBlockRemain -= it->size;
					continue;
				}

				const uint32_t currBlockSize = Log2Ceil(it->size);
				uint32_t level = 0;
				if (currBlockSize > phyBlockSize)
					level = currBlockSize - phyBlockSize;

				levelsCount[level]++;
				currentPhyBlockRemain = (1 << level << phyBlockSize) - it->size;

				for (size_t i = level; i < std::size(levelsCount) - 1; ++i)
				{
					if (levelsCount[level] > 1)
					{
						levelsCount[level] = 0;
						levelsCount[level + 1]++;
					}
				}
			}

			f.flags.pBlock0Count = levelsCount[0];
			f.flags.pBlock1Count = levelsCount[1];
			f.flags.pBlock2Count = levelsCount[2];
			f.flags.pBlock3Count = levelsCount[3];
			f.flags.pBlock4Count = levelsCount[4];
			f.flags.pBlockSize = phyBlockSize - 8;

			return f.uint32;
		}

		auto DumpToFile(HANDLE hFile, Header& header, [[maybe_unused]] BlockList& blockList)
		{
			header.flags = (header.flags & 0xc0000000) | SortAndCalculateFlags(blockList);
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
				uint8_t buf[4096];
				std::fill_n(buf, std::min(size, std::size(buf)), PadByte);
				DeflateWrite(buf, size);
			};

			uint32_t virtualSize = 0;
			for (auto& b : blockList.virtualBlocks)
			{
				DeflateWrite(b.data, b.size);
				auto padSize = RoundUp<16>(b.size) - b.size;
				WritePadBytes(padSize);
				virtualSize += RoundUp<16>(b.size);
			}

			WritePadBytes(4096 - virtualSize);

			for (auto& b : blockList.physicalBlocks)
			{
				DeflateWrite(b.data, b.size);
				auto padSize = RoundUp<16>(b.size) - b.size;
				WritePadBytes(padSize);
			}

			DeflateWrite(nullptr, 0, true);
		}
	}

	template<typename T>
	struct pgArray
	{
		using TInsertContainer = std::vector<T>;

		pgPtrT<T> data;
		uint16_t size;
		uint16_t capacity;

		[[nodiscard]] auto InsertSorted(T value)
		{
			auto ptr = data.Get();
			TInsertContainer container;
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
			TInsertContainer container;
			container.reserve(static_cast<size_t>(size) + 1);
			container.assign(ptr, ptr + size);
			container.insert(container.cbegin() + pos, value);

			data.Set(container.data());
			++size;
			capacity = size;

			return container;
		}

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			blockList.AppendVirtualPtr(data, static_cast<uint32_t>(sizeof(T) * size));
		}
	};
	static_assert(sizeof(pgArray<int>) == 8);

	template<typename T>
	concept HasDumpToMemory = requires (T t, RSC5::BlockList b) {
		t.DumpToMemory(b);
	};

	template<typename T>
	struct pgObjectArray : pgArray<pgPtrT<T>>
	{
		using pgArray<pgPtrT<T>>::data;
		using pgArray<pgPtrT<T>>::size;

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			pgArray<pgPtrT<T>>::DumpToMemory(blockList);

			const auto objsPtr = data.Get();
			for (uint_fast16_t i = 0; i < size; ++i)
			{
				const auto obj = objsPtr[i].Get();
				blockList.AppendVirtual(obj, sizeof(T), &objsPtr[i]);

				if constexpr (HasDumpToMemory<T>)
				{
					obj->DumpToMemory(blockList);
				}
			}
		}
	};
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
		pgPtrT<BlockMap> blockMap;

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			blockList.AppendVirtualPtr(blockMap);
		}
	};
	static_assert(sizeof(pgBase) == 8);

	template<typename T>
	struct pgDictionary : public pgBase
	{
		using THash = pgArray<uint32_t>;
		using TValue = pgObjectArray<T>;
		using THashContainer = THash::TInsertContainer;
		using TValueContainer = TValue::TInsertContainer;
		using TContainer = std::pair<THashContainer, TValueContainer>;

		pgPtrT<pgBase> parent;
		uint32_t usageCount;
		THash hashes;
		TValue values;

		[[nodiscard]] TContainer Insert(uint32_t hash, T* value)
		{
			auto [hashContainer, pos] = hashes.InsertSorted(hash);
			pgPtrT<T> ptr;
			ptr.Set(value);
			auto valueContainer = values.InsertAt(pos, ptr);
			return std::make_pair(std::move(hashContainer), std::move(valueContainer));
		}

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			pgBase::DumpToMemory(blockList);
			hashes.DumpToMemory(blockList);
			values.DumpToMemory(blockList);
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
		pgPtrT<char> name;
		uint32_t nativeHandle;
		uint16_t width;
		uint16_t height;
		D3DFORMAT pixelFormat;
		uint16_t stride;
		uint8_t textureType;
		uint8_t levels;

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			const auto namePtr = name.Get();
			blockList.AppendVirtual(namePtr, static_cast<uint32_t>(strlen(namePtr) + 1), &name);
		}
	};

	struct grcTexturePC : grcTexture
	{
		float unk28[3];
		float unk34[3];
		uint32_t next;
		uint32_t prev;
		pgPtrT<uint8_t, pgPtrBlockType::Physical> pixelData; // In physical data segment
		uint8_t pad[4];

		void DumpToMemory(RSC5::BlockList& blockList)
		{
			grcTexture::DumpToMemory(blockList);

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

			size_t pixelSize = 0;
			size_t w = width;
			size_t h = height;
			for (size_t level = 0; level < levels; ++level)
			{
				size_t rowPitch, slicePitch;
				THROW_IF_FAILED(DirectX::ComputePitch(fmt, w, h, rowPitch, slicePitch));

				pixelSize += slicePitch;

				if (h > 1)
					h >>= 1;

				if (w > 1)
					w >>= 1;
			}

			blockList.AppendPhysicalPtr(pixelData, static_cast<uint32_t>(pixelSize));
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
