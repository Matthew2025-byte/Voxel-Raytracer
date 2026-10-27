#pragma once
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <tuple>

class VoxelRenderer {
   public:
	bool Initialize(SDL_Window* window, uint32_t textureWidth, uint32_t textureHeight,
					const char* computeShaderSource) {
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

		computePipeline = generatePipeline(computeShaderSource);
		if (!computePipeline) {
			SDL_Log("Compute pipeline creation failed: %s", SDL_GetError());
			return false;
		}
		return true;
	}
	void DrawFrame(SDL_Window* window) {
		SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device);
		if (!commandBuffer) return;

		SDL_GPUStorageTextureReadWriteBinding rwBinding = {};
		rwBinding.texture = renderTexture;
		rwBinding.mip_level = 0;
		rwBinding.layer = 0;
		rwBinding.cycle = true;

		SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &rwBinding, 1, NULL, 0);
		SDL_BindGPUComputePipeline(computePass, computePipeline);
		SDL_BindGPUComputeStorageTextures(computePass, 0, &renderTexture, 1);

		SDL_DispatchGPUCompute(computePass, (properties.textureWidth + 15) / 16, (properties.textureHeight + 15) / 16,
							   1);
		SDL_EndGPUComputePass(computePass);

		SDL_GPUTexture* swapchainTexture;
		Uint32 windowW, windowH;
		if (SDL_AcquireGPUSwapchainTexture(commandBuffer, window, &swapchainTexture, &windowW, &windowH)) {
			if (swapchainTexture != NULL) {
				SDL_GPUBlitInfo blitInfo = {};
				blitInfo.source.texture = renderTexture;
				blitInfo.source.w = properties.textureWidth;
				blitInfo.source.h = properties.textureHeight;

				blitInfo.destination.texture = swapchainTexture;
				blitInfo.destination.w = windowW;
				blitInfo.destination.h = windowH;
				blitInfo.filter = SDL_GPU_FILTER_LINEAR;

				SDL_BlitGPUTexture(commandBuffer, &blitInfo);
			}
		}

		SDL_SubmitGPUCommandBuffer(commandBuffer);
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

	SDL_GPUComputePipeline* generatePipeline(const char* computeShaderSource) {
		// create spirv bytecode for shadercross to transpile
		auto [bytecode, bytecodeSize] = generateBytecodeFromHLSL(computeShaderSource);
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
		if (computePipeline) SDL_ReleaseGPUComputePipeline(device, computePipeline);
		if (renderTexture) SDL_ReleaseGPUTexture(device, renderTexture);
		SDL_DestroyGPUDevice(device);
	}
	~VoxelRenderer() { Shutdown(); }

   private:
	struct RenderProperties {
		int textureWidth, textureHeight;
	};
	SDL_GPUDevice* device;
	SDL_GPUComputePipeline* computePipeline;
	SDL_GPUTexture* renderTexture;

	RenderProperties properties;
};
