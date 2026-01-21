import sys
import argparse
import struct

UF2_HEADER_FORMAT = "<IIIIIIII"
UF2_HEADER_SIZE = 32
UF2_BLOCK_SIZE = 512
UF2_PAYLOAD_SIZE = 256
UF2_MAGIC_START = 0x0A324655
UF2_MAGIC_END   = 0x0AB16F30  # Typical end-of-block UF2 marker

# Range to "erase" by creating zero-filled UF2 blocks.
ERASE_START_ADDRESS = 0x101E0000
ERASE_END_ADDRESS   = 0x101FFFFF

class UF2Block:
    def __init__(self, data: bytes):
        self.data = bytearray(data)
        (self.start_magic,
         self.magic2,
         self.flags,
         self.target_addr,
         self.payload_size,
         self.block_num,
         self.num_blocks,
         self.file_container) = struct.unpack(UF2_HEADER_FORMAT, self.data[:UF2_HEADER_SIZE])

    def is_valid_uf2(self) -> bool:
        # Basic sanity: header start magic and end-of-block magic must match
        if self.start_magic != UF2_MAGIC_START:
            return False
        end_marker = struct.unpack_from('<I', self.data, 508)[0]
        if end_marker != UF2_MAGIC_END:
            return False
        # payload_size must reasonably fit in the block (header + payload + end marker <= 512)
        if not (0 < self.payload_size <= 512 - UF2_HEADER_SIZE - 4):
            return False
        return True

    def update_header(self, block_num=None, num_blocks=None):
        if block_num is not None:
            self.block_num = block_num
        if num_blocks is not None:
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
            self.file_container
        )

# Helper to read an entire UF2 file into a list of UF2Block objects.
def read_uf2_blocks(filename: str) -> list[UF2Block]:
    with open(filename, "rb") as f:
        file_data = f.read()

    blocks = []
    total = len(file_data) // UF2_BLOCK_SIZE
    for i in range(total):
        chunk = file_data[i*UF2_BLOCK_SIZE : (i+1)*UF2_BLOCK_SIZE]
        blk = UF2Block(chunk)
        if blk.is_valid_uf2():
            blocks.append(blk)
    return blocks

# Helper to write out a list of UF2Block objects to a single UF2 file.
def write_uf2_blocks(blocks: list[UF2Block], filename: str):
    with open(filename, "wb") as f:
        for blk in blocks:
            f.write(blk.data)


def filter_blocks_before_address(blocks: list[UF2Block], cutoff_addr: int) -> tuple[list[UF2Block], int]:
    """Keep only blocks whose payload lies entirely below cutoff_addr.

    A block is kept if (target_addr + payload_size - 1) < cutoff_addr.
    Returns (filtered_blocks, dropped_count).
    """
    kept: list[UF2Block] = []
    dropped = 0
    for blk in blocks:
        end_addr = blk.target_addr + blk.payload_size - 1
        if end_addr < cutoff_addr:
            kept.append(blk)
        else:
            dropped += 1
    return kept, dropped


def validate_uf2_file(filename: str) -> tuple[bool, str]:
    """Validate a UF2 file structure.

    Checks:
    - File size is a multiple of UF2_BLOCK_SIZE.
    - Each block has valid header magic and end-of-block marker.
    - Block numbering and num_blocks are self-consistent.
    """
    try:
        with open(filename, "rb") as f:
            data = f.read()
    except OSError as e:
        return False, f"Cannot read UF2: {e}"

    if len(data) % UF2_BLOCK_SIZE != 0:
        return False, "Size is not a multiple of UF2 block size"

    total = len(data) // UF2_BLOCK_SIZE
    block_nums = set()
    declared_total = None
    for i in range(total):
        chunk = data[i * UF2_BLOCK_SIZE : (i + 1) * UF2_BLOCK_SIZE]
        try:
            (start_magic, magic2, flags, target_addr, payload_size, block_num, num_blocks, file_container) = struct.unpack(
                UF2_HEADER_FORMAT, chunk[:UF2_HEADER_SIZE]
            )
            end_marker = struct.unpack_from('<I', chunk, 508)[0]
        except struct.error as e:
            return False, f"Block {i} unpack error: {e}"

        if start_magic != UF2_MAGIC_START:
            return False, f"Block {i} has invalid start magic"
        if end_marker != UF2_MAGIC_END:
            return False, f"Block {i} has invalid end marker"
        if not (0 < payload_size <= 512 - UF2_HEADER_SIZE - 4):
            return False, f"Block {i} has invalid payload size {payload_size}"

        block_nums.add(block_num)
        if declared_total is None:
            declared_total = num_blocks
        elif declared_total != num_blocks:
            return False, f"Inconsistent num_blocks in block {i}: {num_blocks} vs {declared_total}"

    # After reading all, ensure numbering matches declared total
    if declared_total != total:
        return False, f"Declared total blocks {declared_total} != actual {total}"
    if len(block_nums) != total:
        return False, f"Non-unique block numbers ({len(block_nums)}/{total})"

    return True, f"Valid UF2 with {total} blocks"

# Create new UF2 blocks covering [start_addr, end_addr] with zeroed payload.
# This effectively "erases" the region in the final combined UF2.
# We'll assign a placeholder block_num=0, num_blocks=0 here.
# We'll fix them later in a final pass.
def create_zeroed_region_blocks(start_addr: int, end_addr: int, flags=0, file_container=0) -> list[UF2Block]:
    blocks = []
    size = (end_addr - start_addr + 1)
    offset = 0

    while size > 0:
        chunk_size = min(UF2_PAYLOAD_SIZE, size)

        block_data = bytearray(UF2_BLOCK_SIZE)
        # Header
        struct.pack_into(
            UF2_HEADER_FORMAT,
            block_data,
            0,
            UF2_MAGIC_START,
            0x9E5D5157,     # magic2 typical
            flags,          # flags
            start_addr + offset,
            chunk_size,
            0,              # block_num placeholder
            0,              # num_blocks placeholder
            file_container  # file_container placeholder
        )

        # For a standard UF2 block, we also place the end-of-block marker
        struct.pack_into('<I', block_data, 508, UF2_MAGIC_END)

        # No need to fill the payload with anything other than zeros.
        # By default it's already zero.

        new_blk = UF2Block(block_data)
        blocks.append(new_blk)

        offset += chunk_size
        size   -= chunk_size

    return blocks


def main():
    parser = argparse.ArgumentParser(description="Combine two UF2 files and optionally append a zeroed erase-region UF2 at the end.")
    parser.add_argument("input1", help="First input UF2")
    parser.add_argument("input2", help="Second input UF2")
    parser.add_argument("output", help="Output UF2 filename")
    parser.add_argument(
        "--include-erase",
        action="store_true",
        help=(
            "Append zeroed UF2 blocks for the erase region "
            f"[0x{ERASE_START_ADDRESS:08X}, 0x{ERASE_END_ADDRESS:08X}] at the end"
        ),
    )

    args = parser.parse_args()

    input1 = args.input1
    input2 = args.input2
    output = args.output

    # 1) Read both UF2 files as blocks.
    uf2_a = read_uf2_blocks(input1)
    uf2_b = read_uf2_blocks(input2)

    # 2) Combine blocks from both inputs in a simple list.
    #    We'll fix the block numbering afterward.
    combined = uf2_a + uf2_b

    # 3) Create the zeroed region for [ERASE_START_ADDRESS, ERASE_END_ADDRESS].
    #    We'll treat it as if it was its own UF2 file (i.e. appended after all else).
    if args.include_erase:
        # 3) Optionally create the zeroed region for [ERASE_START_ADDRESS, ERASE_END_ADDRESS].
        #    We'll treat it as if it was its own UF2 file (appended after all else).
        #    Derive flags/container from the first available input block when possible.
        base_flags = uf2_a[0].flags if uf2_a else (uf2_b[0].flags if uf2_b else 0)
        base_container = uf2_a[0].file_container if uf2_a else (uf2_b[0].file_container if uf2_b else 0)
        erase_region_blocks = create_zeroed_region_blocks(
            ERASE_START_ADDRESS,
            ERASE_END_ADDRESS,
            flags=base_flags,
            file_container=base_container,
        )
        # 4) Append those new blocks at the end.
        combined += erase_region_blocks
    else:
        erase_region_blocks = []
        # Remove any input blocks that land at or beyond ERASE_START_ADDRESS.
        # The output should end before ERASE_START_ADDRESS when not appending the erase region.
        filtered, dropped = filter_blocks_before_address(combined, ERASE_START_ADDRESS)
        combined = filtered

    # 5) Now finalize block numbering. Each block gets a distinct block_num.
    #    Also set num_blocks in every header to the total.
    total_blocks = len(combined)
    for i, blk in enumerate(combined):
        blk.update_header(block_num=i, num_blocks=total_blocks)

    # 6) Write the final UF2.
    write_uf2_blocks(combined, output)

    # 7) Validate the output UF2 file.
    ok, detail = validate_uf2_file(output)
    if ok:
        print(f"Validation: {detail}")
    else:
        print(f"Validation FAILED: {detail}")
        sys.exit(2)

    print(
        (
            f"Combined {len(uf2_a)} blocks + {len(uf2_b)} blocks + {len(erase_region_blocks)} blocks from erase-region = {total_blocks} total.\n"
            f"Zeroed region appended as blocks from 0x{ERASE_START_ADDRESS:08X} to 0x{ERASE_END_ADDRESS:08X}."
        )
        if args.include_erase
        else (
            f"Combined {len(uf2_a)} blocks + {len(uf2_b)} blocks = {total_blocks} total.\n"
            f"No zeroed erase-region appended (default). Output truncated before 0x{ERASE_START_ADDRESS:08X}."
        )
    )
    print("Done.")


if __name__ == "__main__":
    main()
