#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <unordered_map>

#include "VoxelHandler.hpp"
#include "VoxelRenderer.hpp"
#include "utils.hpp"

struct AppState {
	SDL_Window* window;
	VoxelRenderer renderer;

	VoxelOctree::Octree octree;
	SDL_GPUBuffer* octreeBuffer;

	std::unordered_map<SDL_Scancode, bool> keys;
	Camera camera;

	Uint64 lastTick;

	AppState() : lastTick(SDL_GetTicksNS()) {}
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

	app.window = SDL_CreateWindow("VoxelRaytracer", 1280, 720, SDL_WINDOW_RESIZABLE);
	if (!app.window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	if (!app.renderer.Initialize(app.window, 1280, 720)) {
		return SDL_APP_FAILURE;
	}

	// Build and upload octree to gpu
	app.octree.build();
	size_t array_size = sizeof(VoxelOctree::Node) * app.octree.nodes.size();
	SDL_GPUBufferCreateInfo octreeInfo = { 0 };
	octreeInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
	octreeInfo.size = sizeof(VoxelOctree::OctreeHeader) +
					  sizeof(VoxelOctree::Node) * MAX_OCTREE_NODES;	 // Add the size of the metadata
	app.octreeBuffer = SDL_CreateGPUBuffer(app.renderer.getDevice(), &octreeInfo);

	// Create transfer buffer
	SDL_GPUTransferBufferCreateInfo transferInfo = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
													 .size = Uint32(sizeof(VoxelOctree::OctreeHeader) + array_size) };
	SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(app.renderer.getDevice(), &transferInfo);

	// Copy octree data to gpu buffer
	void* map_ptr = SDL_MapGPUTransferBuffer(app.renderer.getDevice(), transferBuffer, false);

	if (map_ptr) {
		uint8_t* byte_ptr = static_cast<uint8_t*>(map_ptr);

		SDL_memcpy(byte_ptr, &app.octree.header, sizeof(VoxelOctree::OctreeHeader));
		SDL_memcpy(byte_ptr + sizeof(VoxelOctree::OctreeHeader), app.octree.nodes.data(), array_size);

		SDL_UnmapGPUTransferBuffer(app.renderer.getDevice(), transferBuffer);
	}

	// Submit transfer buffer via copy pass
	SDL_GPUCommandBuffer* cmd_buffer = SDL_AcquireGPUCommandBuffer(app.renderer.getDevice());
	SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
	SDL_GPUTransferBufferLocation source = { .transfer_buffer = transferBuffer, .offset = 0 };
	SDL_GPUBufferRegion dest = { .buffer = app.octreeBuffer,
								 .offset = 0,
								 .size = Uint32(sizeof(VoxelOctree::OctreeHeader) + array_size) };

	SDL_UploadToGPUBuffer(copy_pass, &source, &dest, false);
	SDL_EndGPUCopyPass(copy_pass);

	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_buffer);
	SDL_WaitForGPUFences(app.renderer.getDevice(), true, &fence, 1);
	SDL_ReleaseGPUFence(app.renderer.getDevice(), fence);
	SDL_ReleaseGPUTransferBuffer(app.renderer.getDevice(), transferBuffer);

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
	AppState& app = *static_cast<AppState*>(appstate);
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
		app.camera.move(event->motion.xrel * 0.01, -event->motion.yrel * 0.01);
	}

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);
	Uint64 currentTick = SDL_GetTicksNS();
	float dt = float(currentTick - app.lastTick) / 1'000'000'000;
	app.lastTick = currentTick;

	float speed = 10 * dt;
	app.camera.gpuBuffer.updateRotation(app.camera.yaw, app.camera.pitch);
	Vector3 step;

	if (app.keys.contains(SDL_SCANCODE_W) && app.keys[SDL_SCANCODE_W]) {
		step += { app.camera.gpuBuffer.forward.x * speed, app.camera.gpuBuffer.forward.y * speed,
				  app.camera.gpuBuffer.forward.z * speed };
	}
	if (app.keys.contains(SDL_SCANCODE_S) && app.keys[SDL_SCANCODE_S]) {
		step -= { app.camera.gpuBuffer.forward.x * speed, app.camera.gpuBuffer.forward.y * speed,
				  app.camera.gpuBuffer.forward.z * speed };
	}
	if (app.keys.contains(SDL_SCANCODE_A) && app.keys[SDL_SCANCODE_A]) {
		step -= { app.camera.gpuBuffer.right.x * speed, step.y -= app.camera.gpuBuffer.right.y * speed,
				  app.camera.gpuBuffer.right.z * speed };
	}
	if (app.keys.contains(SDL_SCANCODE_D) && app.keys[SDL_SCANCODE_D]) {
		step += { app.camera.gpuBuffer.right.x * speed, step.y -= app.camera.gpuBuffer.right.y * speed,
				  app.camera.gpuBuffer.right.z * speed };
	}
	if (app.keys.contains(SDL_SCANCODE_E) && app.keys[SDL_SCANCODE_E]) {
		step +=
			{ app.camera.gpuBuffer.up.x * speed, app.camera.gpuBuffer.up.y * speed, app.camera.gpuBuffer.up.z * speed };
	}
	if (app.keys.contains(SDL_SCANCODE_Q) && app.keys[SDL_SCANCODE_Q]) {
		step -=
			{ app.camera.gpuBuffer.up.x * speed, app.camera.gpuBuffer.up.y * speed, app.camera.gpuBuffer.up.z * speed };
	}
	app.camera.move(step);

	app.renderer.DrawFrame(app.window, app.camera.gpuBuffer, app.octreeBuffer);

	return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
	if (!appstate) {
		return;
	}
	AppState& app = *static_cast<AppState*>(appstate);

	if (app.octreeBuffer) SDL_ReleaseGPUBuffer(app.renderer.getDevice(), app.octreeBuffer);
	if (app.window) SDL_DestroyWindow(app.window);

	delete &app;
	return;
}