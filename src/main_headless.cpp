// Runs a ROM without a window and prints the screen as text.
// Handy for CI, quick checks, and debugging on a machine without SDL.
//
//   chip8-headless roms/demo.ch8 --cycles 2000
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "chip8/Chip8.hpp"
#include "rom_io.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: chip8-headless <rom> [--cycles N] [--vip]\n";
        return 2;
    }
    long cycles = 1000;
    chip8::Quirks quirks;
    for (int a = 2; a < argc; ++a) {
        if (std::strcmp(argv[a], "--cycles") == 0 && a + 1 < argc) {
            cycles = std::strtol(argv[++a], nullptr, 10);
        } else if (std::strcmp(argv[a], "--vip") == 0) {
            quirks = {.shiftUsesVY = true, .loadStoreIncrementsI = true, .jumpWithVX = false, .logicResetsVF = true};
        }
    }

    try {
        chip8::Chip8 emu(quirks, /*seed=*/1);
        emu.loadRom(readRomFile(argv[1]));
        // ~700 instructions per second, timers at 60 Hz: tick every 12 steps.
        for (long c = 0; c < cycles; ++c) {
            emu.step();
            if (c % 12 == 11) emu.tickTimers();
        }
        for (int y = 0; y < chip8::Chip8::kHeight; ++y) {
            std::string line;
            for (int x = 0; x < chip8::Chip8::kWidth; ++x) line += emu.pixel(x, y) ? '#' : '.';
            std::cout << line << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
