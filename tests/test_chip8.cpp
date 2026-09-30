// Opcode-level tests for the CHIP-8 core.
//
// No test framework dependency: each TEST registers itself, CHECK records a
// failure with file/line, and main() runs everything and returns non-zero if
// anything failed (which is what CTest looks at).
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "chip8/Chip8.hpp"

namespace {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

#define TEST(name)                                          \
    static void name();                                     \
    static Registrar registrar_##name(#name, name);         \
    static void name()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
            ++g_failures;                                                               \
        }                                                                               \
    } while (0)

#define CHECK_THROWS(expr)                                                              \
    do {                                                                                \
        bool threw = false;                                                             \
        try { expr; } catch (const chip8::Chip8Error&) { threw = true; }                \
        if (!threw) {                                                                   \
            std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  expected throw: " #expr "\n"; \
            ++g_failures;                                                               \
        }                                                                               \
    } while (0)

using chip8::Chip8;
using chip8::Quirks;

// Load a program made of 16-bit opcodes.
void load(Chip8& emu, std::vector<std::uint16_t> program) {
    std::vector<std::uint8_t> bytes;
    for (auto op : program) {
        bytes.push_back(static_cast<std::uint8_t>(op >> 8));
        bytes.push_back(static_cast<std::uint8_t>(op & 0xFF));
    }
    emu.loadRom(bytes);
}

void steps(Chip8& emu, int n) {
    for (int k = 0; k < n; ++k) emu.step();
}

int litPixels(const Chip8& emu) {
    int count = 0;
    for (bool p : emu.display()) count += p;
    return count;
}

// ---------------------------------------------------------------------------

TEST(font_is_loaded_at_0x050) {
    Chip8 emu;
    CHECK(emu.memory(0x050) == 0xF0);  // top row of "0"
    CHECK(emu.memory(0x050 + 5 * 0xF + 4) == 0x80);  // bottom row of "F"
}

TEST(rom_that_is_too_large_is_rejected) {
    Chip8 emu;
    std::vector<std::uint8_t> big(Chip8::kMaxRomSize + 1, 0);
    CHECK_THROWS(emu.loadRom(big));
}

TEST(load_and_add_immediate_wraps_without_touching_vf) {
    Chip8 emu;
    load(emu, {0x60FF, 0x7002, 0x6F07});  // V0=FF; V0+=2; VF=7
    steps(emu, 3);
    CHECK(emu.v(0) == 0x01);
    CHECK(emu.v(0xF) == 0x07);
}

TEST(add_registers_sets_carry) {
    Chip8 emu;
    load(emu, {0x60F0, 0x6120, 0x8014, 0x6205, 0x6305, 0x8234});
    steps(emu, 3);
    CHECK(emu.v(0) == 0x10);
    CHECK(emu.v(0xF) == 1);
    steps(emu, 3);
    CHECK(emu.v(2) == 0x0A);
    CHECK(emu.v(0xF) == 0);
}

TEST(subtract_sets_not_borrow) {
    Chip8 emu;
    load(emu, {0x6005, 0x6103, 0x8015, 0x6203, 0x6305, 0x8235});
    steps(emu, 3);
    CHECK(emu.v(0) == 2);
    CHECK(emu.v(0xF) == 1);  // no borrow
    steps(emu, 3);
    CHECK(emu.v(2) == 0xFE);
    CHECK(emu.v(0xF) == 0);  // borrow
}

TEST(subn_computes_vy_minus_vx) {
    Chip8 emu;
    load(emu, {0x6003, 0x6108, 0x8017});
    steps(emu, 3);
    CHECK(emu.v(0) == 5);
    CHECK(emu.v(0xF) == 1);
}

TEST(flag_wins_when_vf_is_the_destination) {
    Chip8 emu;
    load(emu, {0x6FFF, 0x6101, 0x8F14});  // VF = FF + 1 -> result 0, carry 1
    steps(emu, 3);
    CHECK(emu.v(0xF) == 1);
}

TEST(shifts_modern_behaviour_uses_vx) {
    Chip8 emu;
    load(emu, {0x6005, 0x61F0, 0x8016, 0x6281, 0x821E});
    steps(emu, 3);
    CHECK(emu.v(0) == 0x02);
    CHECK(emu.v(0xF) == 1);
    steps(emu, 2);
    CHECK(emu.v(2) == 0x02);
    CHECK(emu.v(0xF) == 1);
}

TEST(shifts_vip_quirk_uses_vy) {
    Chip8 emu(Quirks{.shiftUsesVY = true});
    load(emu, {0x6005, 0x6108, 0x8016});  // V0 = V1 >> 1
    steps(emu, 3);
    CHECK(emu.v(0) == 0x04);
    CHECK(emu.v(0xF) == 0);
}

TEST(logic_ops_and_vip_vf_reset) {
    Chip8 plain;
    load(plain, {0x600C, 0x610A, 0x6F09, 0x8011});
    steps(plain, 4);
    CHECK(plain.v(0) == 0x0E);
    CHECK(plain.v(0xF) == 9);

    Chip8 vip(Quirks{.logicResetsVF = true});
    load(vip, {0x600C, 0x610A, 0x6F09, 0x8012});
    steps(vip, 4);
    CHECK(vip.v(0) == 0x08);
    CHECK(vip.v(0xF) == 0);
}

TEST(skip_instructions) {
    Chip8 emu;
    // 3XNN taken, 4XNN not taken, 5XY0 taken, 9XY0 not taken
    load(emu, {0x6042, 0x6142, 0x3042, 0x0000, 0x4042, 0x5010, 0x0000, 0x9010});
    steps(emu, 3);
    CHECK(emu.pc() == 0x208);
    steps(emu, 1);  // 4042: equal, so no skip
    CHECK(emu.pc() == 0x20A);
    steps(emu, 1);  // 5010: equal, skip
    CHECK(emu.pc() == 0x20E);
    steps(emu, 1);  // 9010: equal, no skip
    CHECK(emu.pc() == 0x210);
}

TEST(call_and_return) {
    Chip8 emu;
    load(emu, {0x2206, 0x6001, 0x1204, 0x6002, 0x00EE});
    steps(emu, 1);  // CALL 0x206
    CHECK(emu.pc() == 0x206);
    CHECK(emu.sp() == 1);
    steps(emu, 1);  // 6002 at 0x206
    steps(emu, 1);  // RET
    CHECK(emu.pc() == 0x202);
    CHECK(emu.sp() == 0);
    CHECK(emu.v(0) == 2);
}

TEST(stack_overflow_and_underflow_throw) {
    Chip8 recurse;
    load(recurse, {0x2200});  // calls itself forever
    CHECK_THROWS(steps(recurse, 17));

    Chip8 underflow;
    load(underflow, {0x00EE});
    CHECK_THROWS(steps(underflow, 1));
}

TEST(jump_with_offset_both_modes) {
    Chip8 classic;
    load(classic, {0x6004, 0x6310, 0xB300});
    steps(classic, 3);
    CHECK(classic.pc() == 0x304);  // 0x300 + V0

    Chip8 superchip(Quirks{.jumpWithVX = true});
    load(superchip, {0x6004, 0x6310, 0xB300});
    steps(superchip, 3);
    CHECK(superchip.pc() == 0x310);  // 0x300 + V3
}

TEST(random_is_masked) {
    Chip8 emu(Quirks{}, 1234);
    load(emu, {0xC000, 0xC10F});
    steps(emu, 2);
    CHECK(emu.v(0) == 0);
    CHECK(emu.v(1) <= 0x0F);
}

TEST(draw_sets_pixels_and_detects_collision) {
    Chip8 emu;
    // I = font "0", draw at (0,0) twice: first draws, second erases with collision.
    load(emu, {0x6000, 0xF029, 0xD005, 0xD005});
    steps(emu, 3);
    CHECK(emu.pixel(0, 0));
    CHECK(emu.pixel(3, 0));
    CHECK(!emu.pixel(1, 1));
    CHECK(emu.v(0xF) == 0);
    CHECK(litPixels(emu) == 14);
    steps(emu, 1);
    CHECK(emu.v(0xF) == 1);
    CHECK(litPixels(emu) == 0);
}

TEST(draw_clips_at_edges_but_wraps_start_position) {
    Chip8 edge;
    load(edge, {0x603E, 0x611E, 0xA050, 0xD015});  // (62,30): only a 2x2 corner fits
    steps(edge, 4);
    CHECK(edge.pixel(62, 30));
    CHECK(edge.pixel(63, 30));
    CHECK(!edge.pixel(0, 30));  // no horizontal wrap
    CHECK(!edge.pixel(62, 0));  // no vertical wrap

    Chip8 wrap;
    load(wrap, {0x6042, 0x6100, 0xA050, 0xD011});  // x = 66 wraps to 2
    steps(wrap, 4);
    CHECK(wrap.pixel(2, 0));
}

TEST(clear_screen) {
    Chip8 emu;
    load(emu, {0xA050, 0xD005, 0x00E0});
    steps(emu, 3);
    CHECK(litPixels(emu) == 0);
}

TEST(key_skips) {
    Chip8 emu;
    load(emu, {0x6005, 0xE09E, 0x0000, 0xE0A1});
    emu.setKey(5, true);
    steps(emu, 2);
    CHECK(emu.pc() == 0x206);  // skipped because key 5 is down
    steps(emu, 1);
    CHECK(emu.pc() == 0x208);  // EXA1 does not skip while pressed
}

TEST(wait_for_key_completes_on_release) {
    Chip8 emu;
    load(emu, {0xF30A, 0x6001});
    steps(emu, 1);
    CHECK(emu.waitingForKey());
    steps(emu, 5);
    CHECK(emu.pc() == 0x202);  // stuck
    emu.setKey(0xB, true);
    CHECK(emu.waitingForKey());  // still waiting until release
    emu.setKey(0xB, false);
    CHECK(!emu.waitingForKey());
    CHECK(emu.v(3) == 0xB);
    steps(emu, 1);
    CHECK(emu.v(0) == 1);
}

TEST(timers) {
    Chip8 emu;
    load(emu, {0x6003, 0xF015, 0xF018, 0xF107});
    steps(emu, 3);
    CHECK(emu.delayTimer() == 3);
    CHECK(emu.soundActive());
    emu.tickTimers();
    emu.tickTimers();
    steps(emu, 1);
    CHECK(emu.v(1) == 1);
    emu.tickTimers();
    emu.tickTimers();  // does not go below zero
    CHECK(emu.delayTimer() == 0);
    CHECK(!emu.soundActive());
}

TEST(index_register_ops) {
    Chip8 emu;
    load(emu, {0xA123, 0x6010, 0xF01E, 0x600A, 0xF029});
    steps(emu, 3);
    CHECK(emu.i() == 0x133);
    steps(emu, 2);
    CHECK(emu.i() == 0x050 + 0xA * 5);
}

TEST(bcd) {
    Chip8 emu;
    load(emu, {0x60FE, 0xA300, 0xF033});
    steps(emu, 3);
    CHECK(emu.memory(0x300) == 2);
    CHECK(emu.memory(0x301) == 5);
    CHECK(emu.memory(0x302) == 4);
}

TEST(store_and_load_registers) {
    Chip8 emu;
    load(emu, {0x6011, 0x6122, 0x6233, 0xA300, 0xF255, 0x6000, 0x6100, 0x6200, 0xF265});
    steps(emu, 9);
    CHECK(emu.memory(0x301) == 0x22);
    CHECK(emu.v(0) == 0x11);
    CHECK(emu.v(2) == 0x33);
    CHECK(emu.i() == 0x300);  // modern: I unchanged

    Chip8 vip(Quirks{.loadStoreIncrementsI = true});
    load(vip, {0xA300, 0xF255});
    steps(vip, 2);
    CHECK(vip.i() == 0x303);
}

TEST(unknown_opcodes_throw) {
    Chip8 a;
    load(a, {0x5001});
    CHECK_THROWS(steps(a, 1));
    Chip8 b;
    load(b, {0xE0FF});
    CHECK_THROWS(steps(b, 1));
    Chip8 c;
    load(c, {0xF0FF});
    CHECK_THROWS(steps(c, 1));
    Chip8 d;
    load(d, {0x0123});  // 0NNN machine code call
    CHECK_THROWS(steps(d, 1));
}

TEST(memory_access_past_end_throws) {
    Chip8 emu;
    load(emu, {0xAFFF, 0x6002, 0xF255});  // writes 0xFFF, 0x1000 (out of range)
    CHECK_THROWS(steps(emu, 3));
}

}  // namespace

int main() {
    for (const auto& t : registry()) {
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::cerr << "  EXCEPTION " << e.what() << "\n";
            ++g_failures;
        }
        std::cout << (g_failures == before ? "[ ok ] " : "[FAIL] ") << t.name << "\n";
    }
    std::cout << registry().size() << " tests, " << g_failures << " failed checks\n";
    return g_failures == 0 ? 0 : 1;
}
