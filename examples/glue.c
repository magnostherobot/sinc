#include <stdio.h>

#include <SDL2/SDL.h>

void *on_click(void *x, void *y);

void *init_sdl() {
    return (void *) SDL_Init(SDL_INIT_VIDEO);
}

void *create_window(void *init, void *xv, void *yv, void *wv, void *hv) {
    int x = (int) xv, y = (int) yv, w = (int) wv, h = (int) hv;
    int window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    SDL_Window *window = SDL_CreateWindow("window", x, y, w, h, window_flags);
    int rend_flags = SDL_RENDERER_ACCELERATED;
    SDL_Renderer *rend = SDL_CreateRenderer(window, -1, rend_flags);
    printf("window created\n");
    return (void *) window;
}

void *loop(void *window) {
    int running = 1;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            printf("event\n");
            switch (e.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    SDL_MouseButtonEvent mbe = e.button;
                    if (mbe.button == SDL_BUTTON_LEFT) {
                        on_click((void *) mbe.x, (void *) mbe.y);
                    }
                    break;
                }
            }
        }
    }
    return 0;
}
