#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_shadercross/SDL_shadercross.h>

const char* computeShaderSource = R"(
    RWTexture2D<float4> outTexture : register(u0, space1);

    [numthreads(16, 16, 1)]
    void main(uint3 dispatchThreadID : SV_DispatchThreadID)
    {
        uint width, height;
        outTexture.GetDimensions(width, height);

        if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
            return;

        float r = (float)dispatchThreadID.x / (float)width;
        float g = (float)dispatchThreadID.y / (float)height;
        float b = 0.5f;

        outTexture[dispatchThreadID.xy] = float4(r, g, b, 1.0f);
    }
)";

struct SDLContext {
	SDL_Window* window;
	SDL_GPUDevice* device;
};
struct AppState {
	SDLContext sdl;
	SDL_GPUComputePipeline* computePipeline;
	SDL_GPUTexture* storageTexture;
	int textureWidth;
	int textureHeight;
};
bool CreateComputePipeline(AppState& app) {
	size_t bytecodeSize = 0;
	SDL_ShaderCross_HLSL_Info hlslInfo = {
		.source = computeShaderSource,
		.entrypoint = "main",
		.shader_stage = SDL_ShaderCross_ShaderStage::SDL_SHADERCROSS_SHADERSTAGE_COMPUTE,
	};

	void* bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlslInfo, &bytecodeSize);
	if (!bytecode) {
		SDL_Log("HLSL compilation failed: %s", SDL_GetError());
		return false;
	}

	SDL_ShaderCross_ComputePipelineMetadata* metadata =
		SDL_ShaderCross_ReflectComputeSPIRV(static_cast<const Uint8*>(bytecode), bytecodeSize, 0);
	if (!metadata) {
		SDL_Log("Failed to reflect SPIR-V bytecode: %s", SDL_GetError());
		SDL_free(bytecode);
		return false;
	}

	SDL_ShaderCross_SPIRV_Info spirvInfo = { .bytecode = static_cast<const Uint8*>(bytecode),
											 .bytecode_size = bytecodeSize,
											 .entrypoint = "main",
											 .shader_stage =
												 SDL_ShaderCross_ShaderStage::SDL_SHADERCROSS_SHADERSTAGE_COMPUTE };

	app.computePipeline = SDL_ShaderCross_CompileComputePipelineFromSPIRV(app.sdl.device, &spirvInfo, metadata, 0);

	SDL_free(bytecode);
	SDL_free(metadata);
	if (!app.computePipeline) {
		SDL_Log("Compute pipeline creation failed: %s", SDL_GetError());
		return false;
	}
	return true;
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
	*appstate = new AppState();
	AppState& app = *static_cast<AppState*>(*appstate);

	app.textureWidth = 1280;
	app.textureHeight = 720;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!SDL_ShaderCross_Init()) {
		SDL_Log("Failed to initialize SDL_ShaderCross: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	app.sdl.window = SDL_CreateWindow("VoxelRaytracer", 1280, 720, SDL_WINDOW_RESIZABLE);
	if (!app.sdl.window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	SDL_GPUShaderFormat formats = SDL_ShaderCross_GetHLSLShaderFormats();
	app.sdl.device = SDL_CreateGPUDevice(formats, false, NULL);
	if (!app.sdl.device) {
		SDL_Log("Failed to create GPU device: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_ClaimWindowForGPUDevice(app.sdl.device, app.sdl.window);

	SDL_GPUTextureCreateInfo textureInfo = {
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
		.width = static_cast<uint32_t>(app.textureWidth),
		.height = static_cast<uint32_t>(app.textureHeight),
		.layer_count_or_depth = 1,
		.num_levels = 1,
	};
	app.storageTexture = SDL_CreateGPUTexture(app.sdl.device, &textureInfo);

	if (!CreateComputePipeline(app)) {
		return SDL_APP_FAILURE;
	}

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	AppState& app = *static_cast<AppState*>(appstate);
	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	}

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(app.sdl.device);
	if (!commandBuffer) return SDL_APP_CONTINUE;

	SDL_GPUStorageTextureReadWriteBinding rwBinding = {};
	rwBinding.texture = app.storageTexture;
	rwBinding.mip_level = 0;
	rwBinding.layer = 0;
	rwBinding.cycle = true;

	SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(commandBuffer, &rwBinding, 1, NULL, 0);
	SDL_BindGPUComputePipeline(computePass, app.computePipeline);
	SDL_BindGPUComputeStorageTextures(computePass, 0, &app.storageTexture, 1);

	SDL_DispatchGPUCompute(computePass, (app.textureWidth + 15) / 16, (app.textureHeight + 15) / 16, 1);
	SDL_EndGPUComputePass(computePass);

	SDL_GPUTexture* swapchainTexture;
	Uint32 windowW, windowH;
	if (SDL_AcquireGPUSwapchainTexture(commandBuffer, app.sdl.window, &swapchainTexture, &windowW, &windowH)) {
		if (swapchainTexture != NULL) {
			SDL_GPUBlitInfo blitInfo = {};
			blitInfo.source.texture = app.storageTexture;
			blitInfo.source.w = app.textureWidth;
			blitInfo.source.h = app.textureHeight;

			blitInfo.destination.texture = swapchainTexture;
			blitInfo.destination.w = windowW;
			blitInfo.destination.h = windowH;
			blitInfo.filter = SDL_GPU_FILTER_LINEAR;

			SDL_BlitGPUTexture(commandBuffer, &blitInfo);
		}
	}

	SDL_SubmitGPUCommandBuffer(commandBuffer);
	return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
	AppState& app = *static_cast<AppState*>(appstate);
	if (!appstate) {
		return;
	}

	if (app.sdl.device) {
		if (app.computePipeline) SDL_ReleaseGPUComputePipeline(app.sdl.device, app.computePipeline);
		if (app.storageTexture) SDL_ReleaseGPUTexture(app.sdl.device, app.storageTexture);
		SDL_DestroyGPUDevice(app.sdl.device);
	}
	if (app.sdl.window) SDL_DestroyWindow(app.sdl.window);

	delete appstate;
	return;
}