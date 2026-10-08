#include <SDL.h>
#include <stdint.h>
#include <stdio.h>

#define WIDTH 640
#define HEIGHT 480
#define FRAMES 300
#define WARMUP 10
#define DELAY_MS 16

static uint32_t pixels[WIDTH * HEIGHT];

int main(void)
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window *window = SDL_CreateWindow("sleep baseline", 0, 0, WIDTH, HEIGHT, 0);
  SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
  SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT) : NULL;
  if (!texture || SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT) < 0) {
    fprintf(stderr, "SDL setup: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  uint64_t work_sum = 0, frame_sum = 0, sleep_sum = 0;
  uint64_t sleep_min = UINT64_MAX, sleep_max = 0;
  unsigned early = 0;
  for (unsigned frame = 0; frame < FRAMES + WARMUP; ++frame) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        SDL_Quit();
        return 1;
      }
    }
    uint64_t start = SDL_GetPerformanceCounter();
    for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH; ++x) {
        pixels[y * WIDTH + x] = 0xff000000 | ((x + frame) & 255) << 16 |
            ((y + frame) & 255) << 8 | ((x ^ y) & 255);
      }
    }
    if (SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(*pixels)) < 0 ||
        SDL_RenderClear(renderer) < 0 || SDL_RenderCopy(renderer, texture, NULL, NULL) < 0) {
      fprintf(stderr, "SDL frame: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
    }
    SDL_RenderPresent(renderer);
    uint64_t sleep_start = SDL_GetPerformanceCounter();
    SDL_Delay(DELAY_MS);
    uint64_t end = SDL_GetPerformanceCounter();
    if (frame >= WARMUP) {
      uint64_t slept = end - sleep_start;
      work_sum += sleep_start - start;
      frame_sum += end - start;
      sleep_sum += slept;
      if (slept < sleep_min) sleep_min = slept;
      if (slept > sleep_max) sleep_max = slept;
      if (slept < DELAY_MS * UINT64_C(1000000)) ++early;
    }
  }
  printf("frames=%u delay_ms=%u freq=%llu work_mean_ns=%llu frame_mean_ns=%llu "
      "sleep_mean_ns=%llu sleep_min_ns=%llu sleep_max_ns=%llu early=%u\n",
      FRAMES, DELAY_MS, (unsigned long long)SDL_GetPerformanceFrequency(),
      (unsigned long long)(work_sum / FRAMES), (unsigned long long)(frame_sum / FRAMES),
      (unsigned long long)(sleep_sum / FRAMES), (unsigned long long)sleep_min,
      (unsigned long long)sleep_max, early);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
