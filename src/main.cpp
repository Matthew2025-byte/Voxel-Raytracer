#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <unordered_map>

#include "VoxelRenderer.hpp"
#include "utils.hpp"

const char* computeShaderSource = R"(
    RWTexture2D<float4> outTexture : register(u0, space1);

    cbuffer CameraBuffer : register (b0, space2) {
        float3 cameraPos;
        float3 cameraForward;
        float3 cameraRight;
        float3 cameraUp;
    }

    [numthreads(16, 16, 1)]
    void main(uint3 dispatchThreadID : SV_DispatchThreadID)
    {
        float3 rayOrigin = cameraPos;
        float3 voxelMin = float3(-0.5, -0.5, -0.5);
        float3 voxelMax = float3(0.5, 0.5, 0.5);

        // Threads pixel
        uint width, height;
        outTexture.GetDimensions(width, height);

        uint2 pixel = dispatchThreadID.xy;
        if (pixel.x >= width || pixel.y >= height)
            return;
        
        float2 screenSize = float2(width, height);
        float2 uv = (float2(pixel)) / screenSize * 2.0 - 1.0;

        float fov = radians(60);
        float fovScale = tan(fov * 0.5);
        float aspect = (float)width / (float)height;

        float screenX = uv.x * aspect * fovScale;
        float screenY = -uv.y * fovScale;

        float3 rayDir = normalize((screenX * cameraRight) + (screenY * cameraUp) + cameraForward);

        float3 t0 = (voxelMin - rayOrigin) / rayDir;
        float3 t1 = (voxelMax - rayOrigin) / rayDir;

        float3 tNear = min(t0, t1);
        float3 tFar = max(t0, t1);

        float tEnter = max(max(tNear.x, tNear.y), tNear.z);
        float tExit  = min(min(tFar.x, tFar.y), tFar.z);

        // Pixel color
        float4 pixelColor = float4(0.08, 0.10, 0.14, 1.0);

        if (tEnter <= tExit && tExit >= 0.0) {
            pixelColor = float4(1.0, 0.45, 0.12, 1.0);
        }

        // Write to texture
        outTexture[pixel] = pixelColor;
    }
)";

struct AppState {
	SDL_Window* window;
	VoxelRenderer renderer;

	std::unordered_map<SDL_Scancode, bool> keys;
	Vector3 cameraPos;
	float camRX, camRY;

	Uint64 lastTick;
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
	if (!app.renderer.Initialize(app.window, 1280, 720, computeShaderSource)) {
		return SDL_APP_FAILURE;
	}

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
		app.camRX += event->motion.xrel * 0.01;
		app.camRY -= event->motion.yrel * 0.01;
	}

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);
	Uint64 currentTick = SDL_GetTicksNS();
	float dt = float(currentTick - app.lastTick) / 1'000'000'000;
	app.lastTick = currentTick;

	CameraData camera(app.cameraPos);
	camera.updateRotation(app.camRX, app.camRY);
	float speed = 10 * dt;

	if (app.keys.contains(SDL_SCANCODE_W) && app.keys[SDL_SCANCODE_W]) {
		app.cameraPos.x += camera.forward.x * speed;
		app.cameraPos.y += camera.forward.y * speed;
		app.cameraPos.z += camera.forward.z * speed;
	}
	if (app.keys.contains(SDL_SCANCODE_S) && app.keys[SDL_SCANCODE_S]) {
		app.cameraPos.x -= camera.forward.x * speed;
		app.cameraPos.y -= camera.forward.y * speed;
		app.cameraPos.z -= camera.forward.z * speed;
	}
	if (app.keys.contains(SDL_SCANCODE_A) && app.keys[SDL_SCANCODE_A]) {
		app.cameraPos.x -= camera.right.x * speed;
		app.cameraPos.y -= camera.right.y * speed;
		app.cameraPos.z -= camera.right.z * speed;
	}
	if (app.keys.contains(SDL_SCANCODE_D) && app.keys[SDL_SCANCODE_D]) {
		app.cameraPos.x += camera.right.x * speed;
		app.cameraPos.y += camera.right.y * speed;
		app.cameraPos.z += camera.right.z * speed;
	}
	if (app.keys.contains(SDL_SCANCODE_E) && app.keys[SDL_SCANCODE_E]) {
		app.cameraPos.x += camera.up.x * speed;
		app.cameraPos.y += camera.up.y * speed;
		app.cameraPos.z += camera.up.z * speed;
	}
	if (app.keys.contains(SDL_SCANCODE_Q) && app.keys[SDL_SCANCODE_Q]) {
		app.cameraPos.x -= camera.up.x * speed;
		app.cameraPos.y -= camera.up.y * speed;
		app.cameraPos.z -= camera.up.z * speed;
	}

	app.renderer.DrawFrame(app.window, camera);

	return SDL_APP_CONTINUE;
}
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
	if (!appstate) {
		return;
	}
	AppState& app = *static_cast<AppState*>(appstate);

	app.renderer.Shutdown();
	if (app.window) SDL_DestroyWindow(app.window);

	delete appstate;
	return;
}