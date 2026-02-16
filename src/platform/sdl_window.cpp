#include "platform/sdl_window.hpp"

#if SATURNIS_HAS_SDL2
#include <SDL.h>
#endif

namespace saturnis::platform {

void present_framebuffer_if_available(int width, int height, const std::vector<std::uint32_t> &pixels, bool headless) {
  (void)width;
  (void)height;
  (void)pixels;
  if (headless) {
    return;
  }
#if SATURNIS_HAS_SDL2
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    return;
  }
  SDL_Window *window = SDL_CreateWindow("Saturnis", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
  if (window == nullptr) {
    SDL_Quit();
    return;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  SDL_UpdateTexture(texture, nullptr, pixels.data(), width * static_cast<int>(sizeof(std::uint32_t)));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
  SDL_Delay(300);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
#endif
}

} // namespace saturnis::platform
