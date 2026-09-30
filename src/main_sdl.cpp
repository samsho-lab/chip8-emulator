// SDL2 frontend: window, keyboard, and a square-wave beep.
//
//   chip8 roms/demo.ch8 [--speed 700] [--scale 12] [--vip]
//
// Keypad mapping (the standard layout most emulators use):
//   1 2 3 4        1 2 3 C
//   Q W E R   ->   4 5 6 D
//   A S D F        7 8 9 E
//   Z X C V        A 0 B F
#include <SDL.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "chip8/Chip8.hpp"
#include "rom_io.hpp"

namespace {

int keyFor(SDL_Keycode k) {
    switch (k) {
        case SDLK_1: return 0x1; case SDLK_2: return 0x2; case SDLK_3: return 0x3; case SDLK_4: return 0xC;
        case SDLK_q: return 0x4; case SDLK_w: return 0x5; case SDLK_e: return 0x6; case SDLK_r: return 0xD;
        case SDLK_a: return 0x7; case SDLK_s: return 0x8; case SDLK_d: return 0x9; case SDLK_f: return 0xE;
        case SDLK_z: return 0xA; case SDLK_x: return 0x0; case SDLK_c: return 0xB; case SDLK_v: return 0xF;
        default: return -1;
    }
}

struct Beeper {
    double phase = 0;
    bool on = false;
};

void audioCallback(void* userdata, Uint8* stream, int len) {
    auto* beeper = static_cast<Beeper*>(userdata);
    auto* out = reinterpret_cast<Sint16*>(stream);
    const int samples = len / 2;
    for (int s = 0; s < samples; ++s) {
        out[s] = beeper->on ? (std::fmod(beeper->phase, 1.0) < 0.5 ? 2000 : -2000) : 0;
        beeper->phase += 440.0 / 44100.0;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: chip8 <rom> [--speed IPS] [--scale N] [--vip]\n";
        return 2;
    }
    int speed = 700;
    int scale = 12;
    chip8::Quirks quirks;
    for (int a = 2; a < argc; ++a) {
        if (std::strcmp(argv[a], "--speed") == 0 && a + 1 < argc) speed = std::atoi(argv[++a]);
        else if (std::strcmp(argv[a], "--scale") == 0 && a + 1 < argc) scale = std::atoi(argv[++a]);
        else if (std::strcmp(argv[a], "--vip") == 0)
            quirks = {.shiftUsesVY = true, .loadStoreIncrementsI = true, .jumpWithVX = false, .logicResetsVF = true};
    }

    chip8::Chip8 emu(quirks);
    try {
        emu.loadRom(readRomFile(argv[1]));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("CHIP-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          chip8::Chip8::kWidth * scale, chip8::Chip8::kHeight * scale, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             chip8::Chip8::kWidth, chip8::Chip8::kHeight);

    Beeper beeper;
    SDL_AudioSpec want{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audioCallback;
    want.userdata = &beeper;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    std::array<Uint32, chip8::Chip8::kWidth * chip8::Chip8::kHeight> pixels{};
    const double stepsPerFrame = speed / 60.0;
    double stepBudget = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    const double frameSeconds = 1.0 / 60.0;
    double accumulator = 0;
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                const int key = keyFor(ev.key.keysym.sym);
                if (key >= 0) emu.setKey(key, ev.type == SDL_KEYDOWN);
            }
        }

        // Fixed 60 Hz timestep for timers, independent of the monitor's refresh rate.
        const Uint64 now = SDL_GetPerformanceCounter();
        accumulator += static_cast<double>(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        try {
            while (accumulator >= frameSeconds) {
                stepBudget += stepsPerFrame;
                while (stepBudget >= 1) {
                    emu.step();
                    stepBudget -= 1;
                }
                emu.tickTimers();
                accumulator -= frameSeconds;
            }
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            running = false;
        }

        if (audio) {
            SDL_LockAudioDevice(audio);
            beeper.on = emu.soundActive();
            SDL_UnlockAudioDevice(audio);
        }

        if (emu.drawFlag()) {
            for (std::size_t p = 0; p < pixels.size(); ++p) pixels[p] = emu.display()[p] ? 0xFF33FF66 : 0xFF101418;
            SDL_UpdateTexture(texture, nullptr, pixels.data(), chip8::Chip8::kWidth * sizeof(Uint32));
            emu.clearDrawFlag();
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
