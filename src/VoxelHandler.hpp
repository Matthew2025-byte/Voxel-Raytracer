#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <unordered_map>
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

		(**std::get_if<std::unique_ptr<RawData>>(&data))[i] = v;
	}

	const Voxel* isUniform() const { return std::get_if<Voxel>(&data); }

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
};
}  // namespace Chunking
namespace OctreeHandler {
struct NodeData {
	uint32_t child_pointer;
	uint32_t masks;
};
struct VoxelData {
	uint32_t color;
	uint32_t padding;
};

union Node {
	NodeData node;
	VoxelData voxel;
	Node() : node{ 0, 0 } {}

	static Node makeNode(uint32_t child, uint8_t childMask, uint8_t leafMask) {
		Node n;
		n.node.child_pointer = child;
		n.node.masks = (uint32_t(childMask) << 8) | leafMask;
		return n;
	}
	static Node makeVoxel(uint32_t color) {
		Node n;
		n.voxel = { color, 0 };
		return n;
	}
};
static_assert(sizeof(Node) == 8, "Node must be 64 bits");
struct OctreeHeader {
	Vector3<int> center;
	int half_len;

	uint32_t root_node;
	uint32_t padding[3];

	OctreeHeader() : center{ 0, 0, 0 }, half_len(2), root_node(0), padding{ 0, 0, 0 } {}
};
struct Octree {
	OctreeHeader header;
	std::vector<Node> nodes;

	void buildTestTree() {
		nodes.clear();
		header.root_node = 0;

		// [0]      root: all 8 octants exist, none are leaves
		// [1..8]   one internal node per octant (octant o lives at 1 + o)
		// [9..64]  voxels, 7 per octant (the 8th, the outer corner, is removed)
		nodes.push_back(Node::makeNode(1, 0xFF, 0x00));

		for (uint32_t o = 0; o < 8; o++) {
			// The outer corner of octant o is the child whose bits equal o's bits.
			uint8_t mask = 0xFF & ~(1u << o);
			nodes.push_back(Node::makeNode(9 + o * 7, mask, mask));
		}

		for (uint32_t o = 0; o < 8; o++) {
			for (uint32_t c = 0; c < 8; c++) {
				if (c == o) continue;  // skip the outer corner

				// Voxel coords in the 4x4x4 grid, same bit convention as the shader
				uint32_t x = (o & 1) * 2 + (c & 1);
				uint32_t y = ((o >> 1) & 1) * 2 + ((c >> 1) & 1);
				uint32_t z = ((o >> 2) & 1) * 2 + ((c >> 2) & 1);

				uint32_t color = 0xFF000000u | ((z * 85) << 16) | ((y * 85) << 8) | (x * 85);
				nodes.push_back(Node::makeVoxel(color));
			}
		}
		// nodes.size() == 65
	}
};
}  // namespace OctreeHandler

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

	struct V {
		uint32_t color = Chunking::NULL_VOXEL.color;

		bool operator==(const V&) const = default;
	};
	struct Node {
		bool isLeaf = false;
		union {
			Node* children[8];
			V voxel;
		};
		Node() {
			for (auto& c : children) c = nullptr;
		}
		Node(const Node&) = delete;
		Node& operator=(const Node&) = delete;
		bool compress() {
			if (isLeaf) return voxel.color == Chunking::NULL_VOXEL.color;
			// Run recursive compression on children
			// if child is a leaf, log type
			bool hasChild = false, allLeaves = true, allPresent = true;
			for (auto& c : children) {
				if (!c) {
					allPresent = false;
					continue;
				}
				if (c->compress()) {
					delete c;
					c = nullptr;
					allPresent = false;
					continue;
				}
				hasChild = true;
				if (!c->isLeaf) allLeaves = false;
			}
			if (!hasChild) return true;
			if (allPresent && allLeaves) {
				V refVox = children[0]->voxel;
				for (int i = 1; i < 8; i++)
					if (children[i]->voxel != refVox) return false;
				for (auto child : children) delete child;
				voxel = refVox;
				isLeaf = true;
				return false;
			}
			return false;
		}

		~Node() {
			if (isLeaf) return;
			for (Node* child : children) delete child;
		}
	};

	std::unique_ptr<Node> buildChunk(Chunking::Chunk& chunk) {
		constexpr int chunkSize = Chunking::CHUNK_SIZE;
		using Level = std::vector<Node*>;
		std::vector<Level> levels(Chunking::CHUNK_DEPTH + 1);

		// Build leaf nodes
		levels[Chunking::CHUNK_DEPTH].resize(std::pow(chunkSize, 3));
		for (int z = 0; z < chunkSize; ++z) {
			for (int y = 0; y < chunkSize; ++y) {
				for (int x = 0; x < chunkSize; ++x) {
					Chunking::Voxel voxel = chunk.getVoxel(Vector3<int>(x, y, z));
					if (voxel == Chunking::NULL_VOXEL) continue;

					auto node = new Node();
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
						auto node = new Node();
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
		auto root = std::unique_ptr<Node>(levels[0][0]);
		if (root->compress()) return nullptr;
		return root;
	}
	std::unique_ptr<Node> buildTree(Vector3<int>& outMin, int& outMaxLevels) {
		using Level = std::unordered_map<Vector3<int>, Node*>;

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

		std::vector<Level> levels(maxLevels + 1);

		for (auto& [p, c] : chunks) {
			Node* n = nullptr;
			if (auto* v = c.isUniform()) {
				if (*v == Chunking::NULL_VOXEL) continue;
				n = new Node();
				n->isLeaf = true;
				n->voxel.color = v->color;
			} else {
				c.ensureRaw();
				n = buildChunk(c).release();
			}

			if (n) levels[maxLevels].emplace(p - minChunkPos, n);
		}

		for (int level = maxLevels - 1; level >= 0; level--) {
			for (const auto& [k, child] : levels[level + 1]) {
				Vector3<int> pk(k.x >> 1, k.y >> 1, k.z >> 1);
				uint32_t oct = (k.x & 1) | ((k.y & 1) << 1) | ((k.z & 1) << 2);

				auto [it, inserted] = levels[level].try_emplace(pk, nullptr);
				if (inserted) it->second = new Node();
				it->second->children[oct] = child;
			}
		}

		outMin = minChunkPos;
		outMaxLevels = maxLevels;

		auto it = levels[0].find({ 0, 0, 0 });
		if (it == levels[0].end()) return nullptr;
		auto root = std::unique_ptr<Node>(it->second);
		if (root->compress()) return nullptr;
		return root;
	}

	/**
	 * @brief Builds a linear octree node from the given root node.
	 *
	 * @param root The root node to build the octree node from.
	 * @return OctreeHandler::Octree
	 */
	OctreeHandler::Octree flatten(std::unique_ptr<Node> root) {
		using gpuNode = OctreeHandler::Node;
		OctreeHandler::Octree octree;
		if (!root || root->isLeaf) return octree;

		auto& nodes = octree.nodes;
		std::vector<std::pair<const Node*, uint32_t>> queue;

		nodes.push_back(gpuNode::makeNode(0, 0, 0));
		queue.emplace_back(root.get(), 0u);

		for (size_t q = 0; q < queue.size(); ++q) {
			auto [node, index] = queue[q];

			uint32_t firstChild = (uint32_t)nodes.size();
			uint8_t childMask = 0, leafMask = 0;

			for (int oct = 0; oct < 8; ++oct) {
				const Node* child = node->children[oct];
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

	void buildWorld(Vector3<int> cameraPos) {
		if (!chunkLoader) return;
		for (int cz = cameraPos.z - renderDist; cz <= cameraPos.z + renderDist; ++cz) {
			for (int cx = cameraPos.x - renderDist; cx <= cameraPos.x + renderDist; ++cx) {
				for (int cy = cameraPos.y - renderDist; cy <= cameraPos.y + renderDist; ++cy) {
					Vector3<int> chunkPos = { cx, cy, cz };
					if (chunks.contains(chunkPos)) continue;
					Chunking::Chunk* chunk = chunkLoader(chunkPos);
					if (chunk) chunks.emplace(chunkPos, std::move(*chunk));
				}
			}
		}
	}
	void setChunkLoader(std::function<Chunking::Chunk*(Vector3<int>)> loader) { chunkLoader = std::move(loader); }

	void setRenderDist(int dist) { renderDist = dist; }

   private:
	int renderDist = 8;
	std::unordered_map<Vector3<int>, Chunking::Chunk> chunks;
	std::function<Chunking::Chunk*(Vector3<int>)> chunkLoader;

	static std::tuple<Vector3<int>, Vector3<int>> split(Vector3<int> pos) {
		Vector3<int> chunkPos = { pos.x >> Chunking::CHUNK_DEPTH, pos.y >> Chunking::CHUNK_DEPTH,
								  pos.z >> Chunking::CHUNK_DEPTH };
		Vector3<int> localPos = { pos.x & (Chunking::CHUNK_SIZE - 1), pos.y & (Chunking::CHUNK_SIZE - 1),
								  pos.z & (Chunking::CHUNK_SIZE - 1) };

		return { chunkPos, localPos };
	}
	uint64_t morton(Vector3<uint32_t> pos) {
		uint64_t code = 0;
		for (int i = 0; i < 21; i++) {
			code |= (uint64_t)((pos.x >> i) & 1) << (3 * i + 0);
			code |= (uint64_t)((pos.y >> i) & 1) << (3 * i + 1);
			code |= (uint64_t)((pos.z >> i) & 1) << (3 * i + 2);
		}
		return code;
	}
	Vector3<uint32_t> decode(uint64_t code) {
		Vector3<uint32_t> pos = { 0, 0, 0 };
		for (int i = 0; i < 21; i++) {
			pos.x |= (uint32_t)((code >> (3 * i + 0)) & 1) << i;
			pos.y |= (uint32_t)((code >> (3 * i + 1)) & 1) << i;
			pos.z |= (uint32_t)((code >> (3 * i + 2)) & 1) << i;
		}
		return pos;
	}
};