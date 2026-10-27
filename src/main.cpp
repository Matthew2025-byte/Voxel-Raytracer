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