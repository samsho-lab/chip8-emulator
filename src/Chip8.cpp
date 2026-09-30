#include "chip8/Chip8.hpp"

#include <algorithm>
#include <cstdio>

namespace chip8 {

namespace {

// Built-in hex font: sixteen 4x5 sprites, 5 bytes each, for digits 0-F.
constexpr std::array<std::uint8_t, 80> kFont = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
    0x20, 0x60, 0x20, 0x20, 0x70,  // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
    0xF0, 0x80, 0xF0, 0x80, 0x80,  // F
};

}  // namespace

Chip8::Chip8(Quirks quirks, std::uint32_t seed) : quirks_(quirks), rng_(seed) {
    reset();
}

void Chip8::reset() {
    memory_.fill(0);
    V_.fill(0);
    stack_.fill(0);
    display_.fill(false);
    keys_.fill(false);
    std::copy(kFont.begin(), kFont.end(), memory_.begin() + kFontStart);
    I_ = 0;
    pc_ = kProgramStart;
    sp_ = 0;
    delayTimer_ = 0;
    soundTimer_ = 0;
    drawFlag_ = true;
    waitingForKey_ = false;
    keyRegister_ = 0;
}

void Chip8::loadRom(std::span<const std::uint8_t> rom) {
    if (rom.size() > kMaxRomSize) {
        throw Chip8Error("ROM is " + std::to_string(rom.size()) + " bytes; the limit is " +
                         std::to_string(kMaxRomSize));
    }
    reset();
    std::copy(rom.begin(), rom.end(), memory_.begin() + kProgramStart);
}

void Chip8::step() {
    if (waitingForKey_) {
        return;  // FX0A blocks until setKey() reports a key release.
    }
    const std::uint16_t opcode =
        static_cast<std::uint16_t>(readMemory(pc_) << 8 | readMemory(pc_ + 1));
    pc_ += 2;
    execute(opcode);
}

void Chip8::tickTimers() {
    if (delayTimer_ > 0) --delayTimer_;
    if (soundTimer_ > 0) --soundTimer_;
}

void Chip8::setKey(int key, bool pressed) {
    if (key < 0 || key > 0xF) return;
    const bool wasPressed = keys_[key];
    keys_[key] = pressed;
    // Like the original hardware, FX0A completes on release, not press.
    // Otherwise a single tap would register on several frames in a row.
    if (waitingForKey_ && wasPressed && !pressed) {
        V_[keyRegister_] = static_cast<std::uint8_t>(key);
        waitingForKey_ = false;
    }
}

void Chip8::execute(std::uint16_t opcode) {
    const int x = (opcode >> 8) & 0xF;
    const int y = (opcode >> 4) & 0xF;
    const std::uint8_t n = opcode & 0xF;
    const std::uint8_t nn = opcode & 0xFF;
    const std::uint16_t nnn = opcode & 0xFFF;

    switch (opcode >> 12) {
        case 0x0:
            if (opcode == 0x00E0) {  // CLS
                display_.fill(false);
                drawFlag_ = true;
            } else if (opcode == 0x00EE) {  // RET
                if (sp_ == 0) fail("return with empty stack", opcode);
                pc_ = stack_[--sp_];
            } else {
                // 0NNN calls native RCA 1802 code on the original machine. No
                // interpreter can emulate that, so treat it as an error.
                fail("machine code routine (0NNN) is not supported", opcode);
            }
            break;
        case 0x1:  // JP nnn
            pc_ = nnn;
            break;
        case 0x2:  // CALL nnn
            if (sp_ >= stack_.size()) fail("stack overflow", opcode);
            stack_[sp_++] = pc_;
            pc_ = nnn;
            break;
        case 0x3:  // SE Vx, nn
            if (V_[x] == nn) pc_ += 2;
            break;
        case 0x4:  // SNE Vx, nn
            if (V_[x] != nn) pc_ += 2;
            break;
        case 0x5:  // SE Vx, Vy
            if (n != 0) fail("unknown opcode", opcode);
            if (V_[x] == V_[y]) pc_ += 2;
            break;
        case 0x6:  // LD Vx, nn
            V_[x] = nn;
            break;
        case 0x7:  // ADD Vx, nn (no carry flag)
            V_[x] = static_cast<std::uint8_t>(V_[x] + nn);
            break;
        case 0x8:
            executeArithmetic(opcode, x, y);
            break;
        case 0x9:  // SNE Vx, Vy
            if (n != 0) fail("unknown opcode", opcode);
            if (V_[x] != V_[y]) pc_ += 2;
            break;
        case 0xA:  // LD I, nnn
            I_ = nnn;
            break;
        case 0xB:  // JP V0, nnn  (or BXNN with the SUPER-CHIP quirk)
            pc_ = static_cast<std::uint16_t>((nnn + (quirks_.jumpWithVX ? V_[x] : V_[0])) & 0xFFF);
            break;
        case 0xC: {  // RND Vx, nn
            std::uniform_int_distribution<int> dist(0, 255);
            V_[x] = static_cast<std::uint8_t>(dist(rng_) & nn);
            break;
        }
        case 0xD:  // DRW Vx, Vy, n
            draw(V_[x], V_[y], n);
            break;
        case 0xE:
            if (nn == 0x9E) {  // SKP Vx
                if (keys_[V_[x] & 0xF]) pc_ += 2;
            } else if (nn == 0xA1) {  // SKNP Vx
                if (!keys_[V_[x] & 0xF]) pc_ += 2;
            } else {
                fail("unknown opcode", opcode);
            }
            break;
        case 0xF:
            executeMisc(opcode, x);
            break;
    }
}

void Chip8::executeArithmetic(std::uint16_t opcode, int x, int y) {
    // VF is written last in every case, so that "8xF4"-style instructions
    // that use VF as an operand end with the flag, not the result.
    switch (opcode & 0xF) {
        case 0x0:
            V_[x] = V_[y];
            break;
        case 0x1:
            V_[x] |= V_[y];
            if (quirks_.logicResetsVF) V_[0xF] = 0;
            break;
        case 0x2:
            V_[x] &= V_[y];
            if (quirks_.logicResetsVF) V_[0xF] = 0;
            break;
        case 0x3:
            V_[x] ^= V_[y];
            if (quirks_.logicResetsVF) V_[0xF] = 0;
            break;
        case 0x4: {
            const int sum = V_[x] + V_[y];
            V_[x] = static_cast<std::uint8_t>(sum);
            V_[0xF] = sum > 0xFF ? 1 : 0;
            break;
        }
        case 0x5: {
            const std::uint8_t notBorrow = V_[x] >= V_[y] ? 1 : 0;
            V_[x] = static_cast<std::uint8_t>(V_[x] - V_[y]);
            V_[0xF] = notBorrow;
            break;
        }
        case 0x6: {
            const std::uint8_t src = quirks_.shiftUsesVY ? V_[y] : V_[x];
            V_[x] = static_cast<std::uint8_t>(src >> 1);
            V_[0xF] = src & 0x1;
            break;
        }
        case 0x7: {
            const std::uint8_t notBorrow = V_[y] >= V_[x] ? 1 : 0;
            V_[x] = static_cast<std::uint8_t>(V_[y] - V_[x]);
            V_[0xF] = notBorrow;
            break;
        }
        case 0xE: {
            const std::uint8_t src = quirks_.shiftUsesVY ? V_[y] : V_[x];
            V_[x] = static_cast<std::uint8_t>(src << 1);
            V_[0xF] = (src >> 7) & 0x1;
            break;
        }
        default:
            fail("unknown opcode", opcode);
    }
}

void Chip8::executeMisc(std::uint16_t opcode, int x) {
    switch (opcode & 0xFF) {
        case 0x07:
            V_[x] = delayTimer_;
            break;
        case 0x0A:
            waitingForKey_ = true;
            keyRegister_ = x;
            break;
        case 0x15:
            delayTimer_ = V_[x];
            break;
        case 0x18:
            soundTimer_ = V_[x];
            break;
        case 0x1E:
            I_ = static_cast<std::uint16_t>((I_ + V_[x]) & 0xFFF);
            break;
        case 0x29:  // point I at the font sprite for the low nibble of Vx
            I_ = static_cast<std::uint16_t>(kFontStart + (V_[x] & 0xF) * 5);
            break;
        case 0x33:  // BCD of Vx into I, I+1, I+2
            writeMemory(I_, V_[x] / 100);
            writeMemory(I_ + 1, (V_[x] / 10) % 10);
            writeMemory(I_ + 2, V_[x] % 10);
            break;
        case 0x55:
            for (int r = 0; r <= x; ++r) writeMemory(I_ + r, V_[r]);
            if (quirks_.loadStoreIncrementsI) I_ = static_cast<std::uint16_t>(I_ + x + 1);
            break;
        case 0x65:
            for (int r = 0; r <= x; ++r) V_[r] = readMemory(I_ + r);
            if (quirks_.loadStoreIncrementsI) I_ = static_cast<std::uint16_t>(I_ + x + 1);
            break;
        default:
            fail("unknown opcode", opcode);
    }
}

void Chip8::draw(int xPos, int yPos, int height) {
    // The starting position wraps around the screen, but sprites that run off
    // the edge are clipped rather than wrapped. That matches the VIP and what
    // most test ROMs expect.
    const int x0 = xPos % kWidth;
    const int y0 = yPos % kHeight;
    V_[0xF] = 0;
    for (int row = 0; row < height; ++row) {
        const int py = y0 + row;
        if (py >= kHeight) break;
        const std::uint8_t spriteRow = readMemory(I_ + row);
        for (int bit = 0; bit < 8; ++bit) {
            const int px = x0 + bit;
            if (px >= kWidth) break;
            if (spriteRow & (0x80 >> bit)) {
                bool& cell = display_[py * kWidth + px];
                if (cell) V_[0xF] = 1;  // collision: a lit pixel was turned off
                cell = !cell;
            }
        }
    }
    drawFlag_ = true;
}

std::uint8_t Chip8::readMemory(std::uint16_t addr) const {
    if (addr >= kMemorySize) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "read past end of memory at 0x%04X", addr);
        throw Chip8Error(buf);
    }
    return memory_[addr];
}

void Chip8::writeMemory(std::uint16_t addr, std::uint8_t value) {
    if (addr >= kMemorySize) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "write past end of memory at 0x%04X", addr);
        throw Chip8Error(buf);
    }
    memory_[addr] = value;
}

void Chip8::fail(const std::string& what, std::uint16_t opcode) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s: opcode 0x%04X at 0x%03X", what.c_str(), opcode, pc_ - 2);
    throw Chip8Error(buf);
}

}  // namespace chip8
