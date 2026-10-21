#include "solar/voxel/VoxImport.hpp"

#include "solar/core/Filesystem.hpp"
#include "solar/core/Log.hpp"

#include <cmath>
#include <cstring>

namespace solar::voxel
{
	namespace
	{
		// MagicaVoxel's own limit, and XYZI stores each coordinate in a byte, so nothing past it
		// could be placed anyway. Capping here also keeps the grid allocation from overflowing
		constexpr int maxVoxAxis = 256;

		// A .vox file can come from a mod, so every read is bounds checked here rather than trusted at the call sites
		class ByteReader
		{
		public:
			ByteReader(const uint8_t* in_data, size_t in_size) :
				data(in_data),
				size(in_size)
			{
			}

			bool ReadU32(uint32_t& out_value)
			{
				const uint8_t* bytes = Take(sizeof(uint32_t));

				if (bytes == nullptr)
				{
					return false;
				}

				std::memcpy(&out_value, bytes, sizeof(uint32_t));

				return true;
			}

			bool ReadI32(int32_t& out_value)
			{
				uint32_t raw = 0;

				if (ReadU32(raw) == false)
				{
					return false;
				}

				std::memcpy(&out_value, &raw, sizeof(int32_t));

				return true;
			}

			const uint8_t* Take(size_t in_count)
			{
				if (in_count > size - cursor)
				{
					return nullptr;
				}

				const uint8_t* result = data + cursor;
				cursor += in_count;

				return result;
			}

			bool Skip(size_t in_count)
			{
				return Take(in_count) != nullptr;
			}

			size_t Remaining() const { return size - cursor; }

		private:
			const uint8_t* data = nullptr;
			size_t size = 0;
			size_t cursor = 0;
		};

		struct ChunkHeader
		{
			char id[4] = {};
			uint32_t contentSize = 0;
			uint32_t childrenSize = 0;
		};

		bool ReadChunkHeader(ByteReader& inout_reader, ChunkHeader& out_header)
		{
			const uint8_t* id = inout_reader.Take(4);

			if (id == nullptr)
			{
				return false;
			}

			std::memcpy(out_header.id, id, 4);

			return inout_reader.ReadU32(out_header.contentSize)
				&& inout_reader.ReadU32(out_header.childrenSize);
		}

		bool IsChunk(const ChunkHeader& in_header, const char* in_id)
		{
			return std::memcmp(in_header.id, in_id, 4) == 0;
		}

		float SrgbToLinear(uint8_t in_value)
		{
			const float normalised = in_value / 255.0f;

			return normalised <= 0.04045f
				? normalised / 12.92f
				: std::pow((normalised + 0.055f) / 1.055f, 2.4f);
		}

		void SetDefaultPalette(Palette& out_palette)
		{
			for (size_t index = 1; index < paletteSize; ++index)
			{
				out_palette.colours[index] = Vec4(0.75f, 0.75f, 0.75f, 1.0f);
			}
		}
	}

	Error ImportVox(const uint8_t* in_data, size_t in_size, VoxelGrid& out_grid, Palette& out_palette)
	{
		ByteReader reader(in_data, in_size);

		const uint8_t* magic = reader.Take(4);

		if (magic == nullptr || std::memcmp(magic, "VOX ", 4) != 0)
		{
			SOLAR_LOG_ERR(CORE, "Not a .vox file: missing VOX magic");
			return Error::INVALID_DATA;
		}

		uint32_t version = 0;

		if (reader.ReadU32(version) == false)
		{
			SOLAR_LOG_ERR(CORE, "Truncated .vox file: no version");
			return Error::INVALID_DATA;
		}

		ChunkHeader main;

		if (ReadChunkHeader(reader, main) == false || IsChunk(main, "MAIN") == false)
		{
			SOLAR_LOG_ERR(CORE, "Malformed .vox file: expected a MAIN chunk");
			return Error::INVALID_DATA;
		}

		// MAIN carries everything as children and nothing as content, but the field is honoured
		// rather than assumed, so the loop below starts on a real chunk header either way
		if (reader.Skip(main.contentSize) == false)
		{
			SOLAR_LOG_ERR(CORE, "MAIN chunk claims more content than the file holds");
			return Error::INVALID_DATA;
		}

		IVec3 size = { 0, 0, 0 };
		bool haveSize = false;
		bool haveVoxels = false;
		bool havePalette = false;

		SetDefaultPalette(out_palette);

		// Every chunk shares a header and declares the size of its own content, so each one is
		// read through a reader limited to exactly that and the cursor then lands where the next
		// header must start. Anything unrecognised is therefore skipped for free, and a chunk
		// that turns out to be longer than expected cannot desync the ones after it - which it
		// could when each branch read only what it wanted and left the rest in place.
		// Children are walked as though they were top level, which is all that is needed to find
		// a model inside MAIN
		while (reader.Remaining() >= 12)
		{
			ChunkHeader chunk;

			if (ReadChunkHeader(reader, chunk) == false)
			{
				break;
			}

			const uint8_t* content = reader.Take(chunk.contentSize);

			if (content == nullptr)
			{
				// The id is not printed because it is four raw bytes rather than a string, and a
				// malformed file is exactly where it would not be null terminated
				SOLAR_LOG_ERR(
					CORE,
					"A .vox chunk claims {} bytes of content but the file is shorter",
					chunk.contentSize
				);
				return Error::INVALID_DATA;
			}

			ByteReader body(content, chunk.contentSize);

			// Only the first model in the file is read, so a later SIZE or XYZI is left alone
			// rather than quietly replacing what has already been built
			if (IsChunk(chunk, "SIZE") && haveSize == false)
			{
				if (body.ReadI32(size.x) == false
					|| body.ReadI32(size.y) == false
					|| body.ReadI32(size.z) == false)
				{
					SOLAR_LOG_ERR(CORE, "Truncated SIZE chunk");
					return Error::INVALID_DATA;
				}

				if (size.x <= 0 || size.y <= 0 || size.z <= 0)
				{
					SOLAR_LOG_ERR(CORE, "SIZE chunk has non-positive dimensions");
					return Error::INVALID_DATA;
				}

				if (size.x > maxVoxAxis || size.y > maxVoxAxis || size.z > maxVoxAxis)
				{
					SOLAR_LOG_ERR(
						CORE,
						"SIZE chunk claims {}x{}x{}, over the {} per axis a .vox model can have",
						size.x, size.y, size.z, maxVoxAxis
					);
					return Error::INVALID_DATA;
				}

				haveSize = true;
			}
			else if (IsChunk(chunk, "XYZI") && haveVoxels == false)
			{
				if (haveSize == false)
				{
					SOLAR_LOG_ERR(CORE, "XYZI chunk before SIZE");
					return Error::INVALID_DATA;
				}

				uint32_t voxelCount = 0;

				if (body.ReadU32(voxelCount) == false)
				{
					SOLAR_LOG_ERR(CORE, "Truncated XYZI chunk");
					return Error::INVALID_DATA;
				}

				const uint8_t* records = body.Take(static_cast<size_t>(voxelCount) * 4);

				if (records == nullptr)
				{
					SOLAR_LOG_ERR(CORE, "XYZI chunk claims {} voxels but the file is shorter", voxelCount);
					return Error::INVALID_DATA;
				}

				// MagicaVoxel is Z-up and the engine is Y-up. Mapping (x,y,z) to
				// (x,z,y) would be a reflection and silently mirror the model, so
				// y is flipped to keep it a rotation.
				out_grid.size = { size.x, size.z, size.y };
				out_grid.materials.assign(
					static_cast<size_t>(size.x) * size.y * size.z, 0);

				for (uint32_t index = 0; index < voxelCount; ++index)
				{
					const uint8_t* record = records + index * 4;

					const int voxX = record[0];
					const int voxY = record[1];
					const int voxZ = record[2];
					const uint8_t material = record[3];

					if (voxX >= size.x || voxY >= size.y || voxZ >= size.z)
					{
						continue;
					}

					const int x = voxX;
					const int y = voxZ;
					const int z = size.y - 1 - voxY;

					out_grid.materials[VoxelIndex(out_grid.size, x, y, z)] = material;
				}

				haveVoxels = true;
			}
			else if (IsChunk(chunk, "RGBA") && havePalette == false)
			{
				const uint8_t* colours = body.Take(paletteSize * 4);

				if (colours == nullptr)
				{
					SOLAR_LOG_ERR(CORE, "Truncated RGBA chunk");
					return Error::INVALID_DATA;
				}

				// Material index i reads palette entry i-1. A quirk of the format,
				// and the reason colours come out subtly wrong if ignored.
				for (size_t index = 0; index < paletteSize - 1; ++index)
				{
					const uint8_t* colour = colours + index * 4;

					out_palette.colours[index + 1] = Vec4(
						SrgbToLinear(colour[0]),
						SrgbToLinear(colour[1]),
						SrgbToLinear(colour[2]),
						colour[3] / 255.0f
					);
				}

				havePalette = true;
			}
		}

		if (haveSize == false || haveVoxels == false)
		{
			SOLAR_LOG_ERR(CORE, "No model found in .vox file");
			return Error::INVALID_DATA;
		}

		SOLAR_LOG_INFO(CORE, "Loaded .vox model {}x{}x{} (version {})",
			out_grid.size.x, out_grid.size.y, out_grid.size.z, version);

		return Error::NONE;
	}

	Error ImportVoxFile(const char* in_path, VoxelGrid& out_grid, Palette& out_palette)
	{
		filesystem::Buffer buffer;

		const Error readError = filesystem::ReadFile(in_path, buffer);

		if (readError != Error::NONE)
		{
			return readError;
		}

		return ImportVox(buffer.data, buffer.bytes, out_grid, out_palette);
	}
}
