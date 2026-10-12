/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Optional LCD window for simu-cli. Compiled only when SDL2 is available.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "board.h"
#include "cli_screen.h"
#include "display.h"
#include "simu_control.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
bool g_sdl = false;

int screenScale()
{
  int longer = LCD_W > LCD_H ? LCD_W : LCD_H;
  int scale = 1;
  while (scale < 8 && longer * scale < 480) scale++;
  return scale;
}

void clearColor(uint8_t& r, uint8_t& g, uint8_t& b)
{
  if (LCD_DEPTH < 16) {
    if (isBacklightEnabled()) {
      r = 47;
      g = 123;
      b = 227;
    } else {
      r = 200;
      g = 200;
      b = 200;
    }
  } else {
    r = 0;
    g = 0;
    b = 0;
  }
}

void presentFrame()
{
  uint8_t r = 0, g = 0, b = 0;
  clearColor(r, g, b);
  SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
  SDL_RenderClear(g_renderer);

  refreshDisplay(g_texture);
  SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);

#if LCD_DEPTH == 16
  if (!isBacklightEnabled()) {
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 127);
    SDL_RenderFillRect(g_renderer, nullptr);
  }
#endif

  SDL_RenderPresent(g_renderer);
}

}  // namespace

bool cliScreenInit()
{
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return false;
  }
  g_sdl = true;

  int scale = screenScale();
  g_window = SDL_CreateWindow("EdgeTX simu-cli", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, LCD_W * scale,
                              LCD_H * scale, 0);
  if (!g_window) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    cliScreenShutdown();
    return false;
  }

  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (!g_renderer)
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  if (!g_renderer) {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    cliScreenShutdown();
    return false;
  }

  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
  if (!g_texture) {
    fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
    cliScreenShutdown();
    return false;
  }
  SDL_SetTextureBlendMode(g_texture, SDL_BLENDMODE_BLEND);

  void* pixels = nullptr;
  int pitch = 0;
  if (SDL_LockTexture(g_texture, nullptr, &pixels, &pitch) == 0) {
    for (int y = 0; y < LCD_H; y++)
      memset(static_cast<uint8_t*>(pixels) + y * pitch, 0, LCD_W * 4);
    SDL_UnlockTexture(g_texture);
  }
  return true;
}

void cliScreenRun()
{
  while (!simuControlStopRequested()) {
    Uint64 start = SDL_GetPerformanceCounter();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      bool closed = event.type == SDL_QUIT;
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(g_window))
        closed = true;
      if (closed) simuControlRequestStop();
    }
    if (simuControlStopRequested()) break;

    presentFrame();

    Uint64 end = SDL_GetPerformanceCounter();
    float elapsed =
        (end - start) / static_cast<float>(SDL_GetPerformanceFrequency()) *
        1000.0f;
    int delay = static_cast<int>(std::floor(16.666f - elapsed));
    if (delay > 0) SDL_Delay(delay);
  }
}

void cliScreenShutdown()
{
  if (g_texture) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_renderer) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  if (g_sdl) {
    SDL_Quit();
    g_sdl = false;
  }
}
