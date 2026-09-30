"""Build roms/demo.ch8, a small original demo ROM.

It draws a "CHIP-8" banner and a counter underneath that ticks up about twice
a second. Between them it exercises CLS, CALL/RET, the delay timer, BCD
conversion, the built-in font, register load/store, and XOR drawing (the
counter is erased by drawing it a second time).

This is a tiny two-pass assembler: instructions can refer to labels that are
defined later, and addresses get patched in once everything has a location.
"""
import pathlib

BANNER = {
    "C": [0x78, 0x80, 0x80, 0x80, 0x78],
    "H": [0x88, 0x88, 0xF8, 0x88, 0x88],
    "I": [0x70, 0x20, 0x20, 0x20, 0x70],
    "P": [0xF0, 0x88, 0xF0, 0x80, 0x80],
    "-": [0x00, 0x00, 0x70, 0x00, 0x00],
    "8": [0x70, 0x88, 0x70, 0x88, 0x70],
}


class Asm:
    def __init__(self, origin=0x200):
        self.origin = origin
        self.items = []  # opcodes (int) or (fmt, label) tuples, or raw byte lists
        self.labels = {}

    def here(self):
        size = 0
        for item in self.items:
            size += len(item) if isinstance(item, bytes) else 2
        return self.origin + size

    def label(self, name):
        self.labels[name] = self.here()

    def op(self, opcode):
        self.items.append(opcode)

    def op_label(self, prefix, name):  # e.g. prefix 0x2000 for CALL
        self.items.append((prefix, name))

    def data(self, values):
        self.items.append(bytes(values))

    def assemble(self):
        out = bytearray()
        for item in self.items:
            if isinstance(item, bytes):
                out += item
                continue
            if isinstance(item, tuple):
                prefix, name = item
                item = prefix | self.labels[name]
            out += item.to_bytes(2, "big")
        return bytes(out)


def build():
    a = Asm()
    a.op(0x00E0)                      # CLS

    # --- banner ---------------------------------------------------------
    a.op(0x6106)                      # V1 = 6 (y)
    for i, ch in enumerate("CHIP-8"):
        a.op(0x6000 | (8 + i * 8))    # V0 = x
        a.op_label(0xA000, f"glyph_{ch}")
        a.op(0xD015)                  # DRW V0, V1, 5

    a.op(0x6A00)                      # VA = counter

    # --- main loop --------------------------------------------------------
    a.label("loop")
    a.op_label(0x2000, "digits")      # draw counter
    a.op(0x651E)                      # V5 = 30 ticks (~0.5 s at 60 Hz)
    a.op(0xF515)                      # delay timer = V5
    a.label("wait")
    a.op(0xF507)                      # V5 = delay timer
    a.op(0x3500)                      # skip next if V5 == 0
    a.op_label(0x1000, "wait")
    a.op_label(0x2000, "digits")      # draw again = erase (XOR)
    a.op(0x7A01)                      # VA += 1
    a.op_label(0x1000, "loop")

    # --- subroutine: draw VA as three decimal digits -----------------------
    a.label("digits")
    a.op_label(0xA000, "scratch")
    a.op(0xFA33)                      # BCD(VA) -> [I], [I+1], [I+2]
    a.op(0xF265)                      # V0..V2 = [I..I+2]
    a.op(0x6318)                      # V3 = 24 (x)
    a.op(0x6414)                      # V4 = 20 (y)
    for reg in range(3):
        a.op(0xF029 | reg << 8)       # I = font sprite for V<reg>
        a.op(0xD345)                  # DRW V3, V4, 5
        a.op(0x7306)                  # V3 += 6
    a.op(0x00EE)                      # RET

    # --- data ---------------------------------------------------------------
    for ch, rows in BANNER.items():
        a.label(f"glyph_{ch}")
        a.data(rows)
    a.label("scratch")
    a.data([0, 0, 0])

    return a.assemble()


if __name__ == "__main__":
    rom = build()
    path = pathlib.Path(__file__).resolve().parent.parent / "roms" / "demo.ch8"
    path.write_bytes(rom)
    print(f"wrote {len(rom)} bytes to {path}")
