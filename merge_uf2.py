import sys
import struct
from typing import List

"""
merge_uf2.py (gap-fill version)
------------------------------
Combines two UF2 files so that:

1. **All blocks from the first file** are copied first.
2. **Zero-filled UF2 blocks** are generated to cover the gap between the end of
   the first file and the start of the second file (if any).
3. **All blocks from the second file** are copied after the gap.
4. **Zero-filled UF2 blocks** are generated from the end of the second file up
    to the end of the flash region used here (0x101E0000 inclusive).
5. Block numbers are re-written so that every block from 0x10000000 to
   0x101E0000 is represented and `blockNum` runs continuously from 0 to
   *N - 1* with `numBlocks == N` for every block.

Usage
-----
    python merge_uf2.py <first.uf2> <second.uf2> <output.uf2>

**Assumptions**
* The first UF2 occupies a lower flash region than the second. If the two
  overlap or appear out of order, the script aborts with an error.
* The board is RP2040-style with the region considered from 0x1000_0000 to 0x101E0000.
"""

# ────────── UF2 constants ──────────
UF2_HEADER_FORMAT = "<IIIIIIII"
UF2_HEADER_SIZE   = 32
UF2_BLOCK_SIZE    = 512
UF2_PAYLOAD_SIZE  = 256

UF2_MAGIC_START   = 0x0A324655
UF2_MAGIC_END     = 0x0AB16F30  # Typical trailing marker
UF2_IGNORE_ADDR_FROM = 0x10FFFF00

FLASH_BASE_ADDR   = 0x10000000
FLASH_END_ADDR    = 0x10200000  # Inclusive end address for this build

# ────────── UF2 block helper ──────────
class UF2Block:
    """Represents a single 512-byte UF2 block."""

    def __init__(self, data: bytes):
        self.data = bytearray(data)
        (self.start_magic,
         self.magic2,
         self.flags,
         self.target_addr,
         self.payload_size,
         self.block_num,
         self.num_blocks,
         self.file_container) = struct.unpack(
            UF2_HEADER_FORMAT, self.data[:UF2_HEADER_SIZE]
        )

    # ───── convenience ─────
    def is_valid(self) -> bool:
        return self.start_magic == UF2_MAGIC_START

    def end_addr(self) -> int:
        """Inclusive end address of the payload."""
        return self.target_addr + self.payload_size - 1

    def update_header(self, *, block_num: int, num_blocks: int) -> None:
        """Rewrite `blockNum` and `numBlocks` in place."""
        self.block_num  = block_num
        self.num_blocks = num_blocks
        struct.pack_into(
            UF2_HEADER_FORMAT,
            self.data,
            0,
            self.start_magic,
            self.magic2,
            self.flags,
            self.target_addr,
            self.payload_size,
            self.block_num,
            self.num_blocks,
            self.file_container,
        )

# ────────── file helpers ──────────

def read_uf2_blocks(path: str) -> List[UF2Block]:
    with open(path, "rb") as f:
        raw = f.read()

    blocks: List[UF2Block] = []
    for i in range(len(raw) // UF2_BLOCK_SIZE):
        chunk = raw[i * UF2_BLOCK_SIZE : (i + 1) * UF2_BLOCK_SIZE]
        blk   = UF2Block(chunk)
        if blk.is_valid():
            if blk.target_addr >= UF2_IGNORE_ADDR_FROM:
                continue
            blocks.append(blk)
    return blocks


def write_uf2_blocks(blocks: List[UF2Block], path: str) -> None:
    with open(path, "wb") as f:
        for blk in blocks:
            f.write(blk.data)

# ────────── zero-fill generator ──────────

def create_zero_blocks(start_addr: int, end_addr: int, *, flags: int, file_container: int) -> List[UF2Block]:
    """Return a list of zero-payload UF2 blocks covering the inclusive span."""
    assert start_addr <= end_addr

    blocks: List[UF2Block] = []
    addr = start_addr
    while addr <= end_addr:
        payload_len = min(UF2_PAYLOAD_SIZE, end_addr - addr + 1)

        block_data = bytearray(UF2_BLOCK_SIZE)  # zeros by default
        struct.pack_into(
            UF2_HEADER_FORMAT,
            block_data,
            0,
            UF2_MAGIC_START,
            0x9E5D5157,  # magic2
            flags,
            addr,
            payload_len,
            0,  # placeholder blockNum
            0,  # placeholder numBlocks
            file_container,
        )
        struct.pack_into("<I", block_data, 508, UF2_MAGIC_END)

        blocks.append(UF2Block(block_data))
        addr += payload_len
    return blocks

# ────────── merge routine ──────────

def main() -> None:
    if len(sys.argv) != 4:
        print("Usage: python merge_uf2.py <first.uf2> <second.uf2> <output.uf2>")
        sys.exit(1)

    first_path, second_path, out_path = sys.argv[1:]

    # 1. Load both files
    first_blocks  = read_uf2_blocks(first_path)
    second_blocks = read_uf2_blocks(second_path)

    if not first_blocks or not second_blocks:
        sys.exit("Either input file contains no valid UF2 blocks.")

    first_start  = min(b.target_addr for b in first_blocks)
    first_end    = max(b.end_addr()   for b in first_blocks)
    second_start = min(b.target_addr for b in second_blocks)
    second_end   = max(b.end_addr()   for b in second_blocks)

    # 2. Sanity check order
    if first_end >= second_start:
        sys.exit("The first UF2 overlaps or does not precede the second UF2. Aborting.")

    if second_end > FLASH_END_ADDR:
        sys.exit("Second UF2 exceeds 2 MB flash size.")

    combined: List[UF2Block] = []

    # 3. Append first file
    combined.extend(first_blocks)

    # 4. Zero-fill gap between first and second
    if first_end + 1 <= second_start - 1:
        gap_blocks = create_zero_blocks(
            first_end + 1,
            second_start - 1,
            flags=first_blocks[0].flags,
            file_container=first_blocks[0].file_container,
        )
        combined.extend(gap_blocks)
    else:
        gap_blocks = []

    # 5. Append second file
    combined.extend(second_blocks)

    # 6. Zero-fill after second up to flash end
    if second_end < FLASH_END_ADDR:
        tail_blocks = create_zero_blocks(
            second_end + 1,
            FLASH_END_ADDR,
            flags=first_blocks[0].flags,
            file_container=first_blocks[0].file_container,
        )
        combined.extend(tail_blocks)
    else:
        tail_blocks = []

    # 7. Finalise headers (continuous numbering / total count)
    total_blocks = len(combined)
    for idx, blk in enumerate(combined):
        blk.update_header(block_num=idx, num_blocks=total_blocks)

    # 8. Write out
    write_uf2_blocks(combined, out_path)

    # 9. Stats
    print(
        f"First UF2:  {len(first_blocks)} blocks (0x{first_start:08X}-0x{first_end:08X})",
        f"Gap fill:   {len(gap_blocks)} blocks" if gap_blocks else "Gap fill:   none",
        f"Second UF2: {len(second_blocks)} blocks (0x{second_start:08X}-0x{second_end:08X})",
        f"Tail fill:  {len(tail_blocks)} blocks" if tail_blocks else "Tail fill:  none",
        f"Total:      {total_blocks} blocks",
        sep="\n",
    )


if __name__ == "__main__":
    main()
