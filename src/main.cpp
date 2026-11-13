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

constexpr const char* debug_modes[] = { "Normal", "Iteration Heatmap" };

struct AppState {
	SDL_Window* window;
	VoxelRenderer voxelRenderer;
	Uint64 lastTick;

	std::unordered_map<SDL_Scancode, bool> keys;
	Camera camera;
	float cameraSpeed = 10.0f;

	bool pause = false;

	AppState() : lastTick(SDL_GetTicksNS()), camera({ 0, 0, 0 }, 0, 0) {}
};

void processKey(AppState& app, SDL_Scancode scancode, bool down) {
	app.keys[scancode] = down;
	if (scancode == SDL_SCANCODE_ESCAPE && down) {
		app.pause = !app.pause;
		SDL_SetWindowRelativeMouseMode(app.window, !app.pause);
	}
}

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
	if (SDL_WindowSupportsGPUPresentMode(app.voxelRenderer.getDevice(), app.window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
		SDL_SetGPUSwapchainParameters(app.voxelRenderer.getDevice(), app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
									  SDL_GPU_PRESENTMODE_IMMEDIATE);
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

	app.camera.move({ 0, 64, 0 });

	// Build chunk
	const float noise_scale = 0.003f;
	const float lacunarity = 2.0f;
	const float gain = 0.5f;
	const int octaves = 3;

	const float seed_x = 1234.56f;
	const float seed_z = 7890.12f;
	const int max_height = 128;

	World world;
	world.setRenderDist(4);
	world.setChunkLoader([=](Vector3<int> chunkPos) {
		constexpr int N = Chunking::CHUNK_SIZE;
		auto chunk = new Chunking::Chunk();
		if (chunkPos.y < 0 || chunkPos.y >= max_height / N) return chunk;  // nothing below the ground layer

		for (int lz = 0; lz < N; ++lz) {
			for (int lx = 0; lx < N; ++lx) {
				int x = chunkPos.x * N + lx;
				int z = chunkPos.z * N + lz;

				float noise_val = stb_perlin_fbm_noise3((x + seed_x) * noise_scale, (z + seed_z) * noise_scale, 0.0f,
														lacunarity, gain, octaves);
				float height_percent = std::clamp((noise_val + 1.0f) * 0.5f, 0.0f, 1.0f);
				int surfaceY = (int)(height_percent * max_height);	// computed once per column

				for (int ly = 0; ly < N; ++ly) {
					int wy = chunkPos.y * N + ly;
					if (wy > surfaceY) break;  // everything above is air

					uint32_t g = (uint32_t)(wy / (float)max_height * 255.0f);
					chunk->setVoxel({ lx, ly, lz }, { 0xFF000000u | (g << 16) | (g << 8) | g });
				}
			}
		}
		chunk->computeFaceMask();
		chunk->compress();
		return chunk;
	});
	Uint64 t0 = SDL_GetTicksNS();
	world.update({ 0, 0, 0 }, std::numeric_limits<int>::max());
	Uint64 t1 = SDL_GetTicksNS();
	SDL_Log("World update time: %f ms", (t1 - t0) / 1'000'000.0);
	//  Upload octree to gpu
	app.voxelRenderer.submitOctree(world.buildOctree());
	Uint64 t2 = SDL_GetTicksNS();
	SDL_Log("Octree build time: %f ms", (t2 - t1) / 1'000'000.0);

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
	} else if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) && !event->key.repeat) {
		processKey(app, event->key.scancode, event->type == SDL_EVENT_KEY_DOWN);
	} else if (event->type == SDL_EVENT_MOUSE_MOTION && !app.pause) {
		app.camera.rotate(event->motion.xrel * 0.001, -event->motion.yrel * 0.001);
	}

	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);
	Uint64 currentTick = SDL_GetTicksNS();
	float dt = float(currentTick - app.lastTick) / 1'000'000'000;
	app.lastTick = currentTick;

	ImGui_ImplSDLGPU3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	int currentRenderMode = app.camera.gpuData.debugMode;

	ImGui::Begin("Debug Info", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Frame time: %.3f ms", dt * 1000.0f);
	ImGui::Text("FPS: %.2f", 1.0f / dt);
	if (ImGui::Combo("Render Mode", &currentRenderMode, debug_modes, std::size(debug_modes))) {
		app.camera.gpuData.debugMode = currentRenderMode;
	}
	ImGui::Text("Paused: %s", app.pause ? "Yes" : "No");

	ImGui::Spacing();
	ImGui::SliderFloat("Camera speed", &app.cameraSpeed, 1.0f, 100.0f);
	ImGui::Text("Camera position: (%.2f, %.2f, %.2f)", app.camera.pos.x, app.camera.pos.y, app.camera.pos.z);

	ImGui::Text("World");
	ImGui::DragFloat3("Sun Direction", &app.camera.gpuData.sunDir.x, 0.01f);

	ImGui::End();

	if (!app.pause) {
		if (app.keys.contains(SDL_SCANCODE_W) && app.keys[SDL_SCANCODE_W]) {
			app.camera.move({ app.camera.gpuData.forward * app.cameraSpeed * dt });
		}
		if (app.keys.contains(SDL_SCANCODE_S) && app.keys[SDL_SCANCODE_S]) {
			app.camera.move({ app.camera.gpuData.forward * -app.cameraSpeed * dt });
		}
		if (app.keys.contains(SDL_SCANCODE_A) && app.keys[SDL_SCANCODE_A]) {
			app.camera.move({ app.camera.gpuData.right * -app.cameraSpeed * dt });
		}
		if (app.keys.contains(SDL_SCANCODE_D) && app.keys[SDL_SCANCODE_D]) {
			app.camera.move({ app.camera.gpuData.right * app.cameraSpeed * dt });
		}
		if (app.keys.contains(SDL_SCANCODE_E) && app.keys[SDL_SCANCODE_E]) {
			app.camera.move({ app.camera.gpuData.up * app.cameraSpeed * dt });
		}
		if (app.keys.contains(SDL_SCANCODE_Q) && app.keys[SDL_SCANCODE_Q]) {
			app.camera.move({ app.camera.gpuData.up * -app.cameraSpeed * dt });
		}
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
