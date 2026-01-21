import struct
import argparse

# UF2 Block Header Format
UF2_HEADER_FORMAT = "<IIIIIIII"
UF2_HEADER_SIZE = 32
UF2_BLOCK_SIZE = 512


def read_uf2_file(file_path):
    with open(file_path, "rb") as f:
        while True:
            block = f.read(UF2_BLOCK_SIZE)
            if not block:
                break

            header = block[:UF2_HEADER_SIZE]
            payload = block[UF2_HEADER_SIZE:]

            (
                magicStart0,
                magicStart1,
                flags,
                targetAddr,
                payloadSize,
                blockNo,
                numBlocks,
                fileContainer,
            ) = struct.unpack(UF2_HEADER_FORMAT, header)

            print(f"Block Number: {blockNo + 1}/{numBlocks}")
            print(f"Target Address: {hex(targetAddr)}")
            print(f"Payload Size: {hex(payloadSize)}")
            print(f"Flags: {hex(flags)}")
            print(f"File Container: {hex(fileContainer)}")
            print(f"Payload (first 16 bytes): {[hex(v)for v in payload[:16]]}")
            print("----")


def main():
    parser = argparse.ArgumentParser(
        description="Read and display contents of a UF2 file."
    )
    parser.add_argument("file_path", type=str, help="Full path to the UF2 file")
    args = parser.parse_args()
    read_uf2_file(args.file_path)


if __name__ == "__main__":
    main()
