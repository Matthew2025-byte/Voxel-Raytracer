#pragma once
#include <cstdint>
#include <vector>

#include "utils.hpp"

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

namespace OctreeBuilder {
struct V {
	uint32_t color = 0;

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
		if (isLeaf) return voxel.color == 0;
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
}  // namespace OctreeBuilder