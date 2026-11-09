#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#define STB_PERLIN_IMPLEMENTATION
#include <stb_perlin.h>

#include <unordered_map>

#include "HeightmapLoader.hpp"
#include "VoxelHandler.hpp"
#include "VoxelRenderer.hpp"
#include "utils.hpp"

struct AppState {
	SDL_Window* window;
	VoxelRenderer voxelRenderer;

	std::unordered_map<SDL_Scancode, bool> keys;
	Camera camera;

	Uint64 lastTick;

	AppState() : lastTick(SDL_GetTicksNS()), camera({ 0, 0, -6 }, 0, 0) {}
};

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
	*appstate = new AppState();
	AppState& app = *static_cast<AppState*>(*appstate);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!SDL_ShaderCross_Init()) {
		SDL_Log("Failed to initialize SDL_ShaderCross: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	app.window = SDL_CreateWindow("VoxelRaytracer", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);

	if (!app.window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	int pixelW, pixelH;
	SDL_GetWindowSizeInPixels(app.window, &pixelW, &pixelH);
	SDL_SetWindowSize(app.window, pixelW, pixelH);

	SDL_SetWindowRelativeMouseMode(app.window, true);
	if (!app.voxelRenderer.Initialize(app.window, pixelW, pixelH)) {
		return SDL_APP_FAILURE;
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	ImGui::StyleColorsDark();

	ImGui_ImplSDL3_InitForSDLGPU(app.window);
	ImGui_ImplSDLGPU3_InitInfo initInfo = {};
	initInfo.Device = app.voxelRenderer.getDevice();
	initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(app.voxelRenderer.getDevice(), app.window);
	initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
	ImGui_ImplSDLGPU3_Init(&initInfo);

	// Build chunk
	const float noise_scale = 0.03f;
	const float lacunarity = 2.0f;
	const float gain = 0.5f;
	const int octaves = 3;

	const float seed_x = 1234.56f;
	const float seed_z = 7890.12f;

	World world({ 4, 1, 4 });
	for (int z = 0; z < Chunking::CHUNK_SIZE * 4; ++z) {
		for (int x = 0; x < Chunking::CHUNK_SIZE * 4; ++x) {
			float nx = (static_cast<float>(x) + seed_x) * noise_scale;
			float nz = (static_cast<float>(z) + seed_z) * noise_scale;

			float noise_val = stb_perlin_fbm_noise3(nx, nz, 0.0f, lacunarity, gain, octaves);
			float height_percent = (noise_val + 1.0f) * 0.5f;
			int max_terrain_height = static_cast<int>(height_percent * Chunking::CHUNK_SIZE);

			for (int y = 0; y < Chunking::CHUNK_SIZE; ++y) {
				uint32_t grey_intensity = static_cast<uint32_t>(y / static_cast<float>(Chunking::CHUNK_SIZE) * 255.0f);
				uint32_t grey = (grey_intensity << 16) | (grey_intensity << 8) | grey_intensity;

				if (y > max_terrain_height)
					world.setVoxel({ x, y, z }, Chunking::NULL_VOXEL);
				else
					world.setVoxel({ x, y, z }, { grey });
			}
		}
	}
	// Upload octree to gpu
	app.voxelRenderer.submitOctree(world.buildOctree());

	/*
	 * ============ DEBUG MODE ============
	 * Set 0 for normal rendering
	 * Set 1 for iteration heatmap
	 */
	app.camera.gpuData.debugMode = 0;
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	AppState& app = *static_cast<AppState*>(appstate);
	ImGui_ImplSDL3_ProcessEvent(event);
	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	} else if (event->type == SDL_EVENT_KEY_DOWN) {
		if (event->key.scancode == SDL_SCANCODE_W) app.keys[SDL_SCANCODE_W] = true;
		if (event->key.scancode == SDL_SCANCODE_S) app.keys[SDL_SCANCODE_S] = true;

		if (event->key.scancode == SDL_SCANCODE_A) app.keys[SDL_SCANCODE_A] = true;
		if (event->key.scancode == SDL_SCANCODE_D) app.keys[SDL_SCANCODE_D] = true;

		if (event->key.scancode == SDL_SCANCODE_E) app.keys[SDL_SCANCODE_E] = true;
		if (event->key.scancode == SDL_SCANCODE_Q) app.keys[SDL_SCANCODE_Q] = true;
	} else if (event->type == SDL_EVENT_KEY_UP) {
		if (event->key.scancode == SDL_SCANCODE_W) app.keys[SDL_SCANCODE_W] = false;
		if (event->key.scancode == SDL_SCANCODE_S) app.keys[SDL_SCANCODE_S] = false;

		if (event->key.scancode == SDL_SCANCODE_A) app.keys[SDL_SCANCODE_A] = false;
		if (event->key.scancode == SDL_SCANCODE_D) app.keys[SDL_SCANCODE_D] = false;

		if (event->key.scancode == SDL_SCANCODE_E) app.keys[SDL_SCANCODE_E] = false;
		if (event->key.scancode == SDL_SCANCODE_Q) app.keys[SDL_SCANCODE_Q] = false;
	} else if (event->type == SDL_EVENT_MOUSE_MOTION) {
		app.camera.rotate(event->motion.xrel * 0.001, -event->motion.yrel * 0.001);
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);
	Uint64 currentTick = SDL_GetTicksNS();
	float dt = float(currentTick - app.lastTick) / 1'000'000'000;
	app.lastTick = currentTick;

	float speed = 10 * dt;

	ImGui_ImplSDLGPU3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Frame time: %.3f ms", dt * 1000.0f);
	ImGui::Text("FPS: %.2f", 1.0f / dt);
	ImGui::Text("Render mode: %s", app.camera.gpuData.debugMode == 0 ? "Normal" : "Debug");
	ImGui::End();

	if (app.keys.contains(SDL_SCANCODE_W) && app.keys[SDL_SCANCODE_W]) {
		app.camera.move({ app.camera.gpuData.forward * speed });
	}
	if (app.keys.contains(SDL_SCANCODE_S) && app.keys[SDL_SCANCODE_S]) {
		app.camera.move({ app.camera.gpuData.forward * -speed });
	}
	if (app.keys.contains(SDL_SCANCODE_A) && app.keys[SDL_SCANCODE_A]) {
		app.camera.move({ app.camera.gpuData.right * -speed });
	}
	if (app.keys.contains(SDL_SCANCODE_D) && app.keys[SDL_SCANCODE_D]) {
		app.camera.move({ app.camera.gpuData.right * speed });
	}
	if (app.keys.contains(SDL_SCANCODE_E) && app.keys[SDL_SCANCODE_E]) {
		app.camera.move({ app.camera.gpuData.up * speed });
	}
	if (app.keys.contains(SDL_SCANCODE_Q) && app.keys[SDL_SCANCODE_Q]) {
		app.camera.move({ app.camera.gpuData.up * -speed });
	}

	ImGui::Render();
	ImDrawData* draw_data = ImGui::GetDrawData();

	SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(app.voxelRenderer.getDevice());
	if (!commandBuffer) return SDL_APP_CONTINUE;
	app.voxelRenderer.DrawFrame(app.window, commandBuffer, app.camera.gpuData);

	SDL_GPUTexture* swapchainTexture;
	Uint32 windowW, windowH;
	if (SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, app.window, &swapchainTexture, &windowW, &windowH)) {
		if (swapchainTexture != NULL) {
			SDL_GPUBlitInfo blitInfo = {};
			blitInfo.source.texture = app.voxelRenderer.getRenderTexture();
			blitInfo.source.w = app.voxelRenderer.getProperties().textureWidth;
			blitInfo.source.h = app.voxelRenderer.getProperties().textureHeight;

			blitInfo.destination.texture = swapchainTexture;
			blitInfo.destination.w = windowW;
			blitInfo.destination.h = windowH;
			blitInfo.filter = SDL_GPU_FILTER_LINEAR;

			SDL_BlitGPUTexture(commandBuffer, &blitInfo);

			ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, commandBuffer);
			SDL_GPUColorTargetInfo targetInfo = {};
			targetInfo.texture = swapchainTexture;
			targetInfo.load_op = SDL_GPU_LOADOP_LOAD;
			targetInfo.store_op = SDL_GPU_STOREOP_STORE;

			SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, nullptr);
			ImGui_ImplSDLGPU3_RenderDrawData(draw_data, commandBuffer, renderPass);
			SDL_EndGPURenderPass(renderPass);
		}
		SDL_SubmitGPUCommandBuffer(commandBuffer);
	}
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
	if (!appstate) {
		return;
	}
	AppState& app = *static_cast<AppState*>(appstate);

	SDL_WaitForGPUIdle(app.voxelRenderer.getDevice());
	ImGui_ImplSDL3_Shutdown();
	ImGui_ImplSDLGPU3_Shutdown();
	ImGui::DestroyContext();

	if (app.window) SDL_DestroyWindow(app.window);

	delete &app;
	return;
}
