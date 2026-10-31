#pragma once
#include <vector>

#include "utils.hpp"

namespace VoxelChunks {
struct Voxel {};
struct Chunk {};
}  // namespace VoxelChunks
namespace VoxelOctree {
#define MAX_OCTREE_NODES 100

struct Node {
	uint32_t child_pointer;
	uint32_t masks;

	Node(uint32_t child, uint8_t child_mask, uint8_t leaf_mask) : child_pointer(child) {
		masks = pack_masks(child_mask, leaf_mask);
	}

	uint32_t pack_masks(uint8_t child_mask, uint8_t leaf_mask) {
		return (static_cast<uint32_t>(child_mask) << 8) | leaf_mask;
	}
};
struct OctreeHeader {
	int center[3];
	int half_len;

	uint32_t root_node;
	uint32_t padding[3];

	OctreeHeader() : center{ 0, 0, 0 }, half_len(2), root_node(0), padding{ 0, 0, 0 } {}
};
struct Octree {
	OctreeHeader header;
	std::vector<Node> nodes;

	void build() {
		nodes.clear();
		nodes.push_back(Node(0, 0, 7));
	}
};
}  // namespace VoxelOctree

class VoxelHandler {
   public:
	VoxelHandler() = default;

	void addVoxel(Vector3 pos) {}

   private:
	std::vector<VoxelChunks::Chunk> chunks;

	VoxelOctree::Octree gpuBuffer;
	std::vector<uint32_t> freeSlots;
};