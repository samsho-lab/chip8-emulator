#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <string>

namespace chip8 {

// CHIP-8 interpreters disagree on a few instructions. Different ROMs were
// written against different interpreters, so these are switchable.
struct Quirks {
    // 8XY6 / 8XYE: original COSMAC VIP copies VY into VX before shifting.
    // Most modern interpreters (CHIP-48, SUPER-CHIP) shift VX in place.
    bool shiftUsesVY = false;
    // FX55 / FX65: original VIP leaves I pointing past the last register.
    bool loadStoreIncrementsI = false;
    // BNNN: SUPER-CHIP reads it as BXNN and jumps to XNN + VX instead of NNN + V0.
    bool jumpWithVX = false;
    // 8XY1 / 8XY2 / 8XY3: VIP resets VF to 0 after OR/AND/XOR.
    bool logicResetsVF = false;
};

class Chip8Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Chip8 {
public:
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 32;
    static constexpr std::size_t kMemorySize = 4096;
    static constexpr std::uint16_t kProgramStart = 0x200;
    static constexpr std::uint16_t kFontStart = 0x050;
    static constexpr std::size_t kMaxRomSize = kMemorySize - kProgramStart;

    explicit Chip8(Quirks quirks = {}, std::uint32_t seed = std::random_device{}());

    void reset();
    void loadRom(std::span<const std::uint8_t> rom);

    // Fetch, decode and execute a single instruction.
    void step();

    // Call at 60 Hz, independent of how many instructions run per second.
    void tickTimers();

    void setKey(int key, bool pressed);

    // Read-only views for frontends and tests.
    bool pixel(int x, int y) const { return display_[y * kWidth + x]; }
    const std::array<bool, kWidth * kHeight>& display() const { return display_; }
    bool drawFlag() const { return drawFlag_; }
    void clearDrawFlag() { drawFlag_ = false; }
    bool soundActive() const { return soundTimer_ > 0; }
    bool waitingForKey() const { return waitingForKey_; }

    std::uint8_t v(int index) const { return V_.at(index); }
    std::uint16_t i() const { return I_; }
    std::uint16_t pc() const { return pc_; }
    std::uint8_t sp() const { return sp_; }
    std::uint8_t delayTimer() const { return delayTimer_; }
    std::uint8_t soundTimer() const { return soundTimer_; }
    std::uint8_t memory(std::uint16_t addr) const { return memory_.at(addr); }

    // Test hooks.
    void setV(int index, std::uint8_t value) { V_.at(index) = value; }
    void setI(std::uint16_t value) { I_ = value; }
    void setDelayTimer(std::uint8_t value) { delayTimer_ = value; }

private:
    void execute(std::uint16_t opcode);
    void executeArithmetic(std::uint16_t opcode, int x, int y);
    void executeMisc(std::uint16_t opcode, int x);
    void draw(int x, int y, int height);
    std::uint8_t readMemory(std::uint16_t addr) const;
    void writeMemory(std::uint16_t addr, std::uint8_t value);
    [[noreturn]] void fail(const std::string& what, std::uint16_t opcode) const;

    Quirks quirks_;
    std::mt19937 rng_;

    std::array<std::uint8_t, kMemorySize> memory_{};
    std::array<std::uint8_t, 16> V_{};
    std::array<std::uint16_t, 16> stack_{};
    std::array<bool, kWidth * kHeight> display_{};
    std::array<bool, 16> keys_{};

    std::uint16_t I_ = 0;
    std::uint16_t pc_ = kProgramStart;
    std::uint8_t sp_ = 0;
    std::uint8_t delayTimer_ = 0;
    std::uint8_t soundTimer_ = 0;

    bool drawFlag_ = false;
    bool waitingForKey_ = false;
    int keyRegister_ = 0;
};

}  // namespace chip8
