#include <SDL3/SDL.h>
#include <libmorton/morton.h>

#include <array>
#include <memory>
#include <variant>
#include <vector>

#include "Octree.hpp"
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

	std::vector<OctreeHandler::Node> buildChunk(uint32_t shift) const {
		if (auto* u = isUniform()) {
			return { OctreeHandler::Node::makeVoxel(u->color) };
		}
		if (auto* raw = std::get_if<std::unique_ptr<RawData>>(&data)) {
			std::vector<std::pair<uint32_t, Voxel>> voxels;
			voxels.reserve(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE);

			for (uint32_t m = 0; m < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; ++m) {
				uint_fast16_t x, y, z;
				libmorton::morton3D_32_decode(m, x, y, z);

				uint32_t index = x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
				Voxel v = (**raw)[index];
				if (v == NULL_VOXEL) continue;
				voxels.emplace_back(m, v);
			}

			struct Entry {
				uint32_t code;
				uint32_t firstChild;
				uint32_t color;
				uint8_t childMask = 0;
				uint8_t leafMask = 0;
				bool isLeaf = false;
			};

			std::vector<std::vector<Entry>> levels(CHUNK_DEPTH + 1);
			levels[0].reserve(voxels.size());
			for (auto& [code, voxel] : voxels) levels[0].push_back({ code, 0, voxel.color, 0, 0, true });

			for (uint32_t depth = 0; depth < CHUNK_DEPTH; ++depth) {
				const auto& below = levels[depth];
				auto& above = levels[depth + 1];
				above.reserve(below.size());

				for (uint32_t i = 0; i < below.size();) {
					Entry p{};
					p.code = below[i].code >> 3;
					p.firstChild = i;
					while (i < below.size() && (below[i].code >> 3) == p.code) {
						uint8_t bit = 1u << (below[i].code & 7);
						p.childMask |= bit;
						if (below[i].isLeaf) p.leafMask |= bit;
						++i;
					}
					above.push_back(p);
				}
			}

			std::vector<uint32_t> base(CHUNK_DEPTH + 1);
			uint32_t total = 0;
			for (int d = CHUNK_DEPTH; d >= 0; --d) {
				base[d] = total;
				total += levels[d].size();
			}

			std::vector<OctreeHandler::Node> nodes;
			nodes.reserve(total);
			for (int d = CHUNK_DEPTH; d >= 0; --d) {
				for (const Entry& e : levels[d]) {
					nodes.push_back(e.isLeaf ? OctreeHandler::Node::makeVoxel(e.color)
											 : OctreeHandler::Node::makeNode(shift + base[d - 1] + e.firstChild,
																			 e.childMask, e.leafMask));
				}
			}
			return nodes;
		} else {
			SDL_assert(false && "RLE encoding in a build chunk operation is not capable of run time performance");
		}
	}

   private:
	std::variant<Voxel, std::vector<Run>, std::unique_ptr<RawData>> data{ NULL_VOXEL };
	uint8_t faceMask = 0;
	bool dirty = false, hidden = false;
};
}  // namespace Chunking