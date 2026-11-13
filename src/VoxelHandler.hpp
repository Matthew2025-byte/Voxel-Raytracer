#pragma once
#include <algorithm>
#include <array>
#include <execution>
#include <functional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Chunking.hpp"
#include "Octree.hpp"
#include "utils.hpp"

class World {
   public:
	World() = default;
	Chunking::Voxel getVoxel(Vector3<int> pos) const {
		auto [chunkPos, localPos] = split(pos);

		auto it = chunks.find(chunkPos);
		if (it == chunks.end()) return Chunking::NULL_VOXEL;

		return it->second.getVoxel(localPos);
	}
	Chunking::Chunk* getChunk(Vector3<int> chunkPos) {
		auto it = chunks.find(chunkPos);
		return it == chunks.end() ? nullptr : &it->second;
	}
	void setVoxel(Vector3<int> pos, Chunking::Voxel v) {
		auto [chunkPos, localPos] = split(pos);

		auto it = chunks.find(chunkPos);
		if (it == chunks.end()) return;

		it->second.setVoxel(localPos, v);
	}

	OctreeHandler::Octree buildOctree() {
		int intMax = std::numeric_limits<int>::max();
		Vector3<int> minChunk{ intMax, intMax, intMax };
		for (const auto& [pos, chunk] : chunks) {
			auto* u = chunk.isUniform();
			if (u && *u == Chunking::NULL_VOXEL) continue;
			minChunk.x = std::min(minChunk.x, pos.x);
			minChunk.y = std::min(minChunk.y, pos.y);
			minChunk.z = std::min(minChunk.z, pos.z);
		}
		if (minChunk.x == intMax) return {};

		struct Entry {
			uint64_t code;
			uint32_t firstChild;
			OctreeHandler::Node node;
			uint8_t childMask = 0, leafMask = 0;
			bool isLeaf = false;
		};

		std::vector<OctreeHandler::Node> flat;
		std::vector<std::vector<Entry>> levels(1);
		levels[0].reserve(chunks.size());
		Vector3<int> anyPos{};

		for (auto& [chunkPos, chunk] : chunks) {
			const auto* u = chunk.isUniform();
			if (u && *u == Chunking::NULL_VOXEL) continue;

			if (!u) chunk.ensureRaw();
			auto nodes = chunk.buildChunk((uint32_t)flat.size() - 1);
			if (!u) chunk.compress();
			if (nodes.empty()) continue;

			const Vector3<int> pos = chunkPos - minChunk;
			anyPos = pos;
			const uint64_t code = libmorton::morton3D_64_encode(pos.x, pos.y, pos.z);

			const bool isLeaf = nodes.size() == 1;
			flat.insert(flat.end(), nodes.begin() + 1, nodes.end());
			levels[0].push_back({ code, 0, nodes[0], 0, 0, isLeaf });
		}
		if (levels[0].empty()) return {};

		std::sort(levels[0].begin(), levels[0].end(), [](const Entry& a, const Entry& b) { return a.code < b.code; });

		while (levels.back().size() > 1 || levels.back()[0].isLeaf) {
			const auto& below = levels.back();
			std::vector<Entry> above;
			above.reserve(below.size() / 2 + 1);

			for (uint32_t i = 0; i < below.size();) {
				Entry p{};
				p.code = below[i].code >> 3;
				p.firstChild = i;
				while (i < below.size() && (below[i].code >> 3) == p.code) {
					const uint8_t bit = (uint8_t)(1u << (below[i].code & 7));
					p.childMask |= bit;
					if (below[i].isLeaf) p.leafMask |= bit;
					++i;
				}
				above.push_back(p);
			}
			levels.push_back(std::move(above));
		}
		const int depth = (int)levels.size() - 1;

		std::vector<uint32_t> base(levels.size());
		base[0] = (uint32_t)flat.size();
		for (size_t d = 1; d < levels.size(); ++d) base[d] = base[d - 1] + (uint32_t)levels[d - 1].size();

		flat.reserve(base.back() + levels.back().size());
		for (const Entry& e : levels[0]) flat.push_back(e.node);
		for (size_t d = 1; d < levels.size(); ++d)
			for (const Entry& e : levels[d])
				flat.push_back(OctreeHandler::Node::makeNode(base[d - 1] + e.firstChild, e.childMask, e.leafMask));

		const int halfLen = ((1 << depth) * Chunking::CHUNK_SIZE) / 2;
		const Vector3<int> rootMin =
			minChunk +
			Vector3<int>{ (anyPos.x >> depth) << depth, (anyPos.y >> depth) << depth, (anyPos.z >> depth) << depth };

		OctreeHandler::Octree octree;
		octree.header.root_node = (uint32_t)flat.size() - 1;
		octree.header.half_len = halfLen;
		octree.header.center = rootMin * Chunking::CHUNK_SIZE + Vector3<int>{ halfLen, halfLen, halfLen };
		octree.nodes = std::move(flat);
		return octree;
	}

	bool update(Vector3<int> center, int maxLoads = 4) {
		if (!chunkLoader) return true;

		Vector3<int> cam = {
			center.x >> Chunking::CHUNK_DEPTH,
			center.y >> Chunking::CHUNK_DEPTH,
			center.z >> Chunking::CHUNK_DEPTH,
		};

		if (!hasCamChunk || cam != lastCamChunk) {
			lastCamChunk = cam;
			hasCamChunk = true;
			unloadFarChunks(cam);
			queueMissingChunks(cam);
		}

		for (int i = 0; i < maxLoads && !loadQueue.empty(); ++i) {
			Vector3<int> p = loadQueue.back();
			loadQueue.pop_back();
			if (chunks.contains(p)) continue;

			auto loaded = chunkLoader(p);
			chunks.emplace(p, loaded ? std::move(*loaded) : Chunking::Chunk{});
			if (loaded) octreeDirty = true;
		}
		return loadQueue.empty();
	}
	void setChunkLoader(std::function<Chunking::Chunk*(Vector3<int>)> loader) { chunkLoader = std::move(loader); }

	void setRenderDist(int dist) { renderDist = dist; }

   private:
	int renderDist = 8;
	std::unordered_map<Vector3<int>, Chunking::Chunk> chunks;
	std::function<Chunking::Chunk*(Vector3<int>)> chunkLoader;

	Vector3<int> lastCamChunk{ 0, 0, 0 };
	bool hasCamChunk = false;
	bool octreeDirty = false;
	std::vector<Vector3<int>> loadQueue;

	static std::tuple<Vector3<int>, Vector3<int>> split(Vector3<int> pos) {
		Vector3<int> chunkPos = { pos.x >> Chunking::CHUNK_DEPTH, pos.y >> Chunking::CHUNK_DEPTH,
								  pos.z >> Chunking::CHUNK_DEPTH };
		Vector3<int> localPos = { pos.x & (Chunking::CHUNK_SIZE - 1), pos.y & (Chunking::CHUNK_SIZE - 1),
								  pos.z & (Chunking::CHUNK_SIZE - 1) };

		return { chunkPos, localPos };
	}
	static inline Vector3<int> DIRS[6] = { { 1, 0, 0 },	 { -1, 0, 0 }, { 0, 1, 0 },
										   { 0, -1, 0 }, { 0, 0, 1 },  { 0, 0, -1 } };
	bool computeHidden(Vector3<int> p) const {
		uint8_t covered = 0;
		for (int d = 0; d < 6; ++d) {
			Vector3<int> n = p + DIRS[d];
			if (n.y < 0) {
				covered |= 1 << d;
				continue;
			}
			auto it = chunks.find(n);
			if (it == chunks.end() || (it->second.getFaceMask() & (1 << (d ^ 1)))) {
				covered |= 1 << d;
			}
		}
		return covered == 0x3F;
	}

	void queueMissingChunks(Vector3<int> cam) {
		loadQueue.clear();
		for (int z = -renderDist; z <= renderDist; ++z) {
			for (int y = -renderDist; y <= renderDist; ++y) {
				for (int x = -renderDist; x <= renderDist; ++x) {
					Vector3<int> p = cam + Vector3<int>{ x, y, z };
					if (!chunks.contains(p)) loadQueue.push_back(p);
				}
			}
		}
		std::sort(loadQueue.begin(), loadQueue.end(),
				  [&](const auto& a, const auto& b) { return (a - cam).sqrMagnitude() > (b - cam).sqrMagnitude(); });
	}
	void unloadFarChunks(Vector3<int> cam) {
		int u2 = (renderDist + 2) * (renderDist + 2);
		for (auto it = chunks.begin(); it != chunks.end();) {
			if ((it->first - cam).sqrMagnitude() > u2) {
				auto* u = it->second.isUniform();
				if (!(u && *u == Chunking::NULL_VOXEL)) octreeDirty = true;
				it = chunks.erase(it);
			} else
				++it;
		}
	}
};