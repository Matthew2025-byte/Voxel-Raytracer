#pragma once
#include <algorithm>
#include <array>
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

	std::unique_ptr<OctreeBuilder::Node> buildChunk(Chunking::Chunk& chunk) {
		constexpr int chunkSize = Chunking::CHUNK_SIZE;
		using Level = std::vector<OctreeBuilder::Node*>;
		std::vector<Level> levels(Chunking::CHUNK_DEPTH + 1);

		// Build leaf nodes
		levels[Chunking::CHUNK_DEPTH].resize(std::pow(chunkSize, 3));
		for (int z = 0; z < chunkSize; ++z) {
			for (int y = 0; y < chunkSize; ++y) {
				for (int x = 0; x < chunkSize; ++x) {
					Chunking::Voxel voxel = chunk.getVoxel(Vector3<int>(x, y, z));
					if (voxel == Chunking::NULL_VOXEL) continue;

					auto node = new OctreeBuilder::Node();
					node->isLeaf = true;
					node->voxel.color = voxel.color;
					levels[Chunking::CHUNK_DEPTH][x + y * chunkSize + z * chunkSize * chunkSize] = node;
				}
			}
		}

		// Build parent nodes
		int currentLevel = Chunking::CHUNK_DEPTH - 1;
		int half = 1;
		while (currentLevel >= 0) {
			int currentSize = 1 << currentLevel;
			levels[currentLevel].resize(currentSize * currentSize * currentSize);

			int childSize = 1 << (currentLevel + 1);
			for (int z = 0; z < currentSize; ++z) {
				for (int y = 0; y < currentSize; ++y) {
					for (int x = 0; x < currentSize; ++x) {
						auto node = new OctreeBuilder::Node();
						bool hasChildren = false;
						for (int oct = 0; oct < 8; oct++) {
							int cx = (x * 2) + (oct & 1);
							int cy = (y * 2) + ((oct >> 1) & 1);
							int cz = (z * 2) + ((oct >> 2) & 1);
							auto it = levels[currentLevel + 1][cx + cy * childSize + cz * childSize * childSize];

							if (it == nullptr) continue;  // air
							node->children[oct] = it;
							hasChildren = true;
						}
						if (!hasChildren) {
							delete node;
							continue;
						}
						size_t parentIdx = x + y * currentSize + z * currentSize * currentSize;
						levels[currentLevel][parentIdx] = node;
					}
				}
			}
			currentLevel--;
		}
		if (levels[0][0] == nullptr) return nullptr;
		auto root = std::unique_ptr<OctreeBuilder::Node>(levels[0][0]);
		if (root->compress()) return nullptr;
		return root;
	}
	std::unique_ptr<OctreeBuilder::Node> buildTree(Vector3<int>& outMin, int& outMaxLevels) {
		using Level = std::unordered_map<Vector3<int>, OctreeBuilder::Node*>;

		Vector3<int> minChunkPos = chunks.begin()->first;
		Vector3<int> maxChunkPos = minChunkPos;
		for (const auto& [chunkPos, chunk] : chunks) {
			minChunkPos.x = std::min(minChunkPos.x, chunkPos.x);
			minChunkPos.y = std::min(minChunkPos.y, chunkPos.y);
			minChunkPos.z = std::min(minChunkPos.z, chunkPos.z);
			maxChunkPos.x = std::max(maxChunkPos.x, chunkPos.x);
			maxChunkPos.y = std::max(maxChunkPos.y, chunkPos.y);
			maxChunkPos.z = std::max(maxChunkPos.z, chunkPos.z);
		}
		Vector3<int> delta = maxChunkPos - minChunkPos;
		int extent = std::max(delta.x, std::max(delta.y, delta.z)) + 1;
		int maxLevels = 0;
		while ((1 << maxLevels) < extent) maxLevels++;

		Uint64 t0 = SDL_GetTicksNS();
		std::vector<Level> levels(maxLevels + 1);
		int solid = 0, hidden = 0;
		for (auto& [p, c] : chunks) {
			OctreeBuilder::Node* n = nullptr;
			// Calculate if chunk is hidden and skip
			if (c.getFaceMask() == 0x3F) {
				solid++;
			}
			if (computeHidden(p)) {
				hidden++;
				continue;
			}
			if (auto* v = c.isUniform()) {
				if (*v == Chunking::NULL_VOXEL) continue;
				n = new OctreeBuilder::Node();
				n->isLeaf = true;
				n->voxel.color = v->color;
			} else {
				c.ensureRaw();
				n = buildChunk(c).release();
			}

			if (n) levels[maxLevels].emplace(p - minChunkPos, n);
		}
		Uint64 t1 = SDL_GetTicksNS();
		SDL_Log("chunks %zu, solid faces %d, hidden %d, time %.3f ms", chunks.size(), solid, hidden,
				(t1 - t0) / 1000000.0);

		for (int level = maxLevels - 1; level >= 0; level--) {
			for (const auto& [k, child] : levels[level + 1]) {
				Vector3<int> pk(k.x >> 1, k.y >> 1, k.z >> 1);
				uint32_t oct = (k.x & 1) | ((k.y & 1) << 1) | ((k.z & 1) << 2);

				auto [it, inserted] = levels[level].try_emplace(pk, nullptr);
				if (inserted) it->second = new OctreeBuilder::Node();
				it->second->children[oct] = child;
			}
		}

		outMin = minChunkPos;
		outMaxLevels = maxLevels;

		auto it = levels[0].find({ 0, 0, 0 });
		if (it == levels[0].end()) return nullptr;
		auto root = std::unique_ptr<OctreeBuilder::Node>(it->second);
		if (root->compress()) return nullptr;
		return root;
	}

	/**
	 * @brief Builds a linear octree node from the given root node.
	 *
	 * @param root The root node to build the octree node from.
	 * @return OctreeHandler::Octree
	 */
	OctreeHandler::Octree flatten(std::unique_ptr<OctreeBuilder::Node> root) {
		using gpuNode = OctreeHandler::Node;
		OctreeHandler::Octree octree;
		if (!root || root->isLeaf) return octree;

		auto& nodes = octree.nodes;
		std::vector<std::pair<const OctreeBuilder::Node*, uint32_t>> queue;

		nodes.push_back(gpuNode::makeNode(0, 0, 0));
		queue.emplace_back(root.get(), 0u);

		for (size_t q = 0; q < queue.size(); ++q) {
			auto [node, index] = queue[q];

			uint32_t firstChild = (uint32_t)nodes.size();
			uint8_t childMask = 0, leafMask = 0;

			for (int oct = 0; oct < 8; ++oct) {
				const OctreeBuilder::Node* child = node->children[oct];
				if (!child) continue;

				childMask |= 1u << oct;
				if (child->isLeaf) {
					leafMask |= 1u << oct;
					nodes.push_back(gpuNode::makeVoxel(child->voxel.color));
				} else {
					queue.emplace_back(child, (uint32_t)nodes.size());
					nodes.push_back(gpuNode::makeNode(0, 0, 0));
				}
			}
			nodes[index] = gpuNode::makeNode(firstChild, childMask, leafMask);
		}

		return octree;
	}
	OctreeHandler::Octree buildOctree() {
		Vector3<int> minChunk;
		int maxLevels;

		Uint64 t0 = SDL_GetTicksNS();
		auto root = buildTree(minChunk, maxLevels);
		Uint64 t1 = SDL_GetTicksNS();
		OctreeHandler::Octree octree = flatten(std::move(root));
		Uint64 t2 = SDL_GetTicksNS();
		SDL_Log("gen %.0f ms, build %.0f ms", (t1 - t0) / 1e6, (t2 - t1) / 1e6);

		int worldDepth = Chunking::CHUNK_DEPTH + maxLevels;
		int halfLen = (1 << worldDepth) / 2;

		octree.header.half_len = halfLen;
		octree.header.center = { minChunk.x * Chunking::CHUNK_SIZE + halfLen,
								 minChunk.y * Chunking::CHUNK_SIZE + halfLen,
								 minChunk.z * Chunking::CHUNK_SIZE + halfLen };
		octree.header.root_node = 0;

		SDL_Log("Built octree with %zu nodes, depth %d", octree.nodes.size(), worldDepth);
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