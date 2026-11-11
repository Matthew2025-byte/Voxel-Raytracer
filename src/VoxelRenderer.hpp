#pragma once
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <string>
#include <tuple>

#include "VoxelHandler.hpp"
#include "utils.hpp"

struct CameraData {
	Vector3f pos;
	float pad1;
	Vector3f forward;
	float pad2;
	Vector3f right;
	float pad3;
	Vector3f up;
	uint32_t debugMode;

	CameraData() : pos(0, 0, 0), forward(0, 0, 1), right(1, 0, 0), up(0, 1, 0), debugMode(0) {}
	CameraData(Vector3f pos) : pos(pos), forward(0, 0, 1), right(1, 0, 0), up(0, 1, 0), debugMode(0) {}
};
static_assert(sizeof(CameraData) == 64, "CameraData must be 64 bytes");
static_assert(offsetof(CameraData, forward) == 16, "forward must start at byte 16");
struct Camera {
	CameraData gpuData;
	Vector3f pos = { 0, 0, 0 };
	Quaternion rotation = { 1, 0, 0, 0 };

	Camera(Vector3f pos, float yaw, float pitch) : pos(pos) { rotate(yaw, pitch); }

	void move(const Vector3f& position) {
		pos += position;
		gpuData.pos += position;
	}
	void rotate(const float& yaw, const float& pitch) {
		Quaternion yawRotation = Quaternion::fromAxisAngle({ 0, 1, 0 }, yaw);
		Quaternion pitchRotation = Quaternion::fromAxisAngle({ 1, 0, 0 }, -pitch);

		rotation = (yawRotation * rotation * pitchRotation).normalized();

		gpuData.forward = rotation.rotate({ 0, 0, 1 });
		gpuData.right = rotation.rotate({ 1, 0, 0 });
		gpuData.up = rotation.rotate({ 0, 1, 0 });
	}
};
class VoxelRenderer {
   public:
	struct RenderProperties {
		int textureWidth, textureHeight;
	};
	bool Initialize(SDL_Window* window, uint32_t textureWidth, uint32_t textureHeight) {
		this->properties.textureWidth = textureWidth;
		this->properties.textureHeight = textureHeight;
		SDL_GPUShaderFormat formats = SDL_ShaderCross_GetHLSLShaderFormats();
		device = SDL_CreateGPUDevice(formats, false, NULL);
		if (!device) {
			SDL_Log("Failed to create GPU device: %s", SDL_GetError());
			return false;
		}
		SDL_ClaimWindowForGPUDevice(device, window);

		SDL_GPUTextureCreateInfo textureInfo = {
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
			.width = static_cast<uint32_t>(textureWidth),
			.height = static_cast<uint32_t>(textureHeight),
			.layer_count_or_depth = 1,
			.num_levels = 1,
		};
		renderTexture = SDL_CreateGPUTexture(device, &textureInfo);

		computePipeline = generatePipeline();
		if (!computePipeline) {
			SDL_Log("Compute pipeline creation failed: %s", SDL_GetError());
			return false;
		}
		return true;
	}
	void DrawFrame(SDL_Window* window, SDL_GPUCommandBuffer* commandBuffer, CameraData camera) {
		SDL_GPUStorageTextureReadWriteBinding rwBinding = {};
		rwBinding.texture = renderTexture;

		SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &rwBinding, 1, NULL, 0);
		SDL_BindGPUComputePipeline(computePass, computePipeline);

		SDL_BindGPUComputeStorageBuffers(computePass, 0, &octreeBuffer, 1);
		SDL_PushGPUComputeUniformData(commandBuffer, 0, &camera, sizeof(camera));

		SDL_DispatchGPUCompute(computePass, (properties.textureWidth + 15) / 16, (properties.textureHeight + 15) / 16,
							   1);
		SDL_EndGPUComputePass(computePass);
	}
	void submitOctree(const OctreeHandler::Octree& octree) {
		size_t array_size = sizeof(OctreeHandler::Node) * octree.nodes.size();
		SDL_GPUBufferCreateInfo octreeInfo = { 0 };
		octreeInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
		octreeInfo.size = sizeof(OctreeHandler::OctreeHeader) + array_size;	 // Add the size of the metadata
		if (octreeBuffer == nullptr) octreeBuffer = SDL_CreateGPUBuffer(device, &octreeInfo);
		if (octreeTransferBuffer == nullptr) {
			// Create transfer buffer
			SDL_GPUTransferBufferCreateInfo transferInfo = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
															 .size = Uint32(sizeof(OctreeHandler::OctreeHeader) +
																			array_size) };
			octreeTransferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
			if (!octreeTransferBuffer) {
				SDL_Log("An error occured creating the transfer buffer: %s", SDL_GetError());
				return;
			} else
				SDL_Log("Created transfer buffer");
		}
		// Copy octree data to gpu buffer
		void* map_ptr = SDL_MapGPUTransferBuffer(device, octreeTransferBuffer, false);

		if (map_ptr) {
			uint8_t* byte_ptr = static_cast<uint8_t*>(map_ptr);

			SDL_memcpy(byte_ptr, &octree.header, sizeof(OctreeHandler::OctreeHeader));
			SDL_memcpy(byte_ptr + sizeof(OctreeHandler::OctreeHeader), octree.nodes.data(), array_size);

			SDL_UnmapGPUTransferBuffer(device, octreeTransferBuffer);
		}

		// Submit transfer buffer via copy pass
		SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(device);
		SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
		SDL_GPUTransferBufferLocation source = { .transfer_buffer = octreeTransferBuffer, .offset = 0 };
		SDL_GPUBufferRegion dest = { .buffer = octreeBuffer,
									 .offset = 0,
									 .size = Uint32(sizeof(OctreeHandler::OctreeHeader) + array_size) };

		SDL_UploadToGPUBuffer(copy_pass, &source, &dest, false);
		SDL_EndGPUCopyPass(copy_pass);

		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_buffer);
		SDL_WaitForGPUFences(device, true, &fence, 1);
		SDL_ReleaseGPUFence(device, fence);
	}
	std::tuple<void*, size_t> generateBytecodeFromHLSL(const char* computeShaderSource) {
		size_t bytecodeSize = 0;
		SDL_ShaderCross_HLSL_Info hlslInfo = {
			.source = computeShaderSource,
			.entrypoint = "main",
			.shader_stage = SDL_ShaderCross_ShaderStage::SDL_SHADERCROSS_SHADERSTAGE_COMPUTE,
		};

		void* bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlslInfo, &bytecodeSize);
		return std::tuple(bytecode, bytecodeSize);
	}

	SDL_GPUComputePipeline* generatePipeline() {
		// create spirv bytecode for shadercross to transpile
		size_t file_size;
		void* file_data = SDL_LoadFile("renderer.comp.hlsl", &file_size);
		if (!file_data) {
			SDL_Log("Unable to load file: %s", SDL_GetError());
			return nullptr;
		}

		auto [bytecode, bytecodeSize] = generateBytecodeFromHLSL((const char*)file_data);
		SDL_free(file_data);
		if (!bytecode) {
			SDL_Log("Unable to generate bytecode: %s", SDL_GetError());
			return nullptr;
		}

		// create renderer compute shader metadata from properties derrived from the bytecode
		SDL_ShaderCross_ComputePipelineMetadata* metadata =
			SDL_ShaderCross_ReflectComputeSPIRV(static_cast<const Uint8*>(bytecode), bytecodeSize, 0);
		if (!metadata) {
			SDL_Log("Failed to reflect SPIR-V bytecode: %s", SDL_GetError());
			SDL_free(bytecode);
			return nullptr;
		}

		SDL_ShaderCross_SPIRV_Info spirvInfo = { 0 };
		spirvInfo.bytecode = static_cast<const Uint8*>(bytecode);
		spirvInfo.bytecode_size = bytecodeSize;
		spirvInfo.entrypoint = "main";
		spirvInfo.shader_stage = SDL_ShaderCross_ShaderStage::SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;

		SDL_GPUComputePipeline* computePipeline =
			SDL_ShaderCross_CompileComputePipelineFromSPIRV(device, &spirvInfo, metadata, 0);

		SDL_free(bytecode);
		SDL_free(metadata);
		return computePipeline;
	}

	void Shutdown() {
		if (!device) return;
		if (octreeTransferBuffer) SDL_ReleaseGPUTransferBuffer(device, octreeTransferBuffer);
		if (octreeBuffer) SDL_ReleaseGPUBuffer(device, octreeBuffer);
		if (computePipeline) SDL_ReleaseGPUComputePipeline(device, computePipeline);
		if (renderTexture) SDL_ReleaseGPUTexture(device, renderTexture);
		SDL_DestroyGPUDevice(device);
	}
	~VoxelRenderer() { Shutdown(); }

	SDL_GPUDevice* getDevice() const { return device; }
	SDL_GPUTexture* getRenderTexture() const { return renderTexture; }
	RenderProperties getProperties() const { return properties; }

   private:
	SDL_GPUDevice* device;
	SDL_GPUBuffer* octreeBuffer = nullptr;
	SDL_GPUTransferBuffer* octreeTransferBuffer = nullptr;
	SDL_GPUComputePipeline* computePipeline;
	SDL_GPUTexture* renderTexture;

	RenderProperties properties;
};
