#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <variant>
#include <vector>

#include "utils.hpp"

namespace Chunking {
constexpr int CHUNK_DEPTH = 5;
constexpr int CHUNK_SIZE = 1 << CHUNK_DEPTH;
struct Voxel {
	uint32_t color;

	bool operator==(const Voxel& other) const { return color == other.color; }
};
constexpr Voxel NULL_VOXEL = Voxel{ 0 };
struct Chunk {
	using RawData = std::array<Voxel, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE>;
	struct Run {
		uint32_t length;
		Voxel voxel;
	};
	Chunk() = default;

	size_t index(Vector3<int> pos) const {
		SDL_assert(pos.x >= 0 && pos.x < CHUNK_SIZE);
		SDL_assert(pos.y >= 0 && pos.y < CHUNK_SIZE);
		SDL_assert(pos.z >= 0 && pos.z < CHUNK_SIZE);
		return pos.x + pos.y * CHUNK_SIZE + pos.z * CHUNK_SIZE * CHUNK_SIZE;
	}
	const Voxel& getVoxel(Vector3<int> pos) const {
		size_t i = index(pos);
		if (auto* uniform = std::get_if<Voxel>(&data)) return *uniform;
		if (auto* r = std::get_if<std::vector<Run>>(&data)) {
			size_t end = 0;
			for (const Run& run : *r) {
				end += run.length;
				if (i < end) return run.voxel;
			}
			return NULL_VOXEL;
		}
		return (**std::get_if<std::unique_ptr<RawData>>(&data))[i];
	}
	void setVoxel(Vector3<int> pos, Voxel v) {
		size_t i = index(pos);
		if (auto* u = std::get_if<Voxel>(&data); u && *u == v) return;
		ensureRaw();
		(*std::get<std::unique_ptr<RawData>>(data))[i] = v;

		if (pos.x == 0 || pos.x == CHUNK_SIZE || pos.y == 0 || pos.y == CHUNK_SIZE || pos.z == 0 || pos.z == CHUNK_SIZE)
			computeFaceMask();
	}

	const Voxel* isUniform() const { return std::get_if<Voxel>(&data); }
	void setHidden(bool hidden) { this->hidden = hidden; }
	bool isHidden() { return hidden; }
	void computeFaceMask() {
		if (auto* u = isUniform()) {
			faceMask = (*u == NULL_VOXEL) ? 0 : 0x3F;
			return;
		}
		ensureRaw();
		const RawData& raw = **std::get_if<std::unique_ptr<RawData>>(&data);
		constexpr int S = CHUNK_SIZE, L = S - 1;
		auto air = [&](int x, int y, int z) { return raw[x + y * S + z * S * S] == NULL_VOXEL; };

		uint8_t mask = 0x3F;
		for (int a = 0; a < S && mask; ++a) {
			for (int b = 0; b < S; ++b) {
				if (air(L, a, b)) mask &= ~1;
				if (air(0, a, b)) mask &= ~2;
				if (air(a, L, b)) mask &= ~4;
				if (air(a, 0, b)) mask &= ~8;
				if (air(a, b, L)) mask &= ~16;
				if (air(a, b, 0)) mask &= ~32;
			}
		}
		faceMask = mask;
	}
	uint8_t getFaceMask() const { return faceMask; }

	void ensureRaw() {
		if (std::holds_alternative<std::unique_ptr<RawData>>(data)) return;
		auto raw = std::make_unique<RawData>();
		if (auto* uniform = std::get_if<Voxel>(&data))
			raw->fill(*uniform);
		else {
			size_t k = 0;
			for (const Run& run : *std::get_if<std::vector<Run>>(&data))
				for (uint32_t j = 0; j < run.length; ++j) {
					(*raw)[k++] = run.voxel;
				}
		}
		data = std::move(raw);
	}
	void compress() {
		auto* rawPtr = std::get_if<std::unique_ptr<RawData>>(&data);
		if (!rawPtr) return;
		const RawData& raw = **rawPtr;

		std::vector<Run> runs;
		for (const Voxel& v : raw) {
			if (!runs.empty() && runs.back().voxel == v)
				runs.back().length++;
			else
				runs.push_back({ 1, v });
		}
		if (runs.size() == 1)
			data = runs[0].voxel;
		else if (runs.size() * sizeof(Run) < sizeof(RawData) / 2)
			data = std::move(runs);
	}

	size_t size() const {
		if (auto* runs = std::get_if<std::vector<Run>>(&data)) return sizeof(Run) * runs->size();
		if (std::holds_alternative<std::unique_ptr<RawData>>(data)) return sizeof(RawData);
		return sizeof(Voxel);
	}

   private:
	std::variant<Voxel, std::vector<Run>, std::unique_ptr<RawData>> data{ NULL_VOXEL };
	uint8_t faceMask;
	bool dirty, hidden;
};

}  // namespace Chunking