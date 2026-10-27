#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include "VoxelRenderer.hpp"

const char* computeShaderSource = R"(
    RWTexture2D<float4> outTexture : register(u0, space1);

    [numthreads(16, 16, 1)]
    void main(uint3 dispatchThreadID : SV_DispatchThreadID)
    {
        float3 cameraPos = float3(0.0, 0.0, -3.0);
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
        float2 uv = (float2(pixel) - screenSize * 0.5) / (screenSize * 0.5);

        float fov = radians(60);
        float fovScale = tan(fov * 0.5);

        float aspect = (float)width / (float)height;

        float3 rayDir = float3(uv.x * aspect * fovScale, -uv.y * fovScale, 1.0);
        rayDir = normalize(rayDir);

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
	}

	return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppIterate(void* appstate) {
	AppState& app = *static_cast<AppState*>(appstate);

	app.renderer.DrawFrame(app.window);

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