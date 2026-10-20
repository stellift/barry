import argparse
import os
import struct
import subprocess
import sys

SECTOR_SIZE = 512
PARTITION_START_LBA = 2048
EFI_SYSTEM_PARTITION_TYPE = 0xEF
MBR_PARTITION_TABLE_OFFSET = 0x1BE
MBR_SIGNATURE_OFFSET = 0x1FE


def build_mbr(total_sectors):
    partition_sectors = total_sectors - PARTITION_START_LBA
    entry = struct.pack(
        "<BBBBBBBBII",
        0x80, 0xFE, 0xFF, 0xFF,
        EFI_SYSTEM_PARTITION_TYPE, 0xFE, 0xFF, 0xFF,
        PARTITION_START_LBA, partition_sectors,
    )
    mbr = bytearray(SECTOR_SIZE)
    mbr[MBR_PARTITION_TABLE_OFFSET:MBR_PARTITION_TABLE_OFFSET + len(entry)] = entry
    mbr[MBR_SIGNATURE_OFFSET:MBR_SIGNATURE_OFFSET + 2] = b"\x55\xAA"
    return bytes(mbr)


def run(command):
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--size-mib", type=int, default=64)
    parser.add_argument("--add", action="append", default=[],
                        metavar="LOCAL:IMAGE", help="copy LOCAL into the image at IMAGE")
    arguments = parser.parse_args()

    total_sectors = arguments.size_mib * 1024 * 1024 // SECTOR_SIZE

    with open(arguments.output, "wb") as image:
        image.truncate(arguments.size_mib * 1024 * 1024)
        image.write(build_mbr(total_sectors))

    run(["mkfs.vfat", "-F", "32", "-n", "BARRY", "--offset", str(PARTITION_START_LBA),
         arguments.output])

    image_spec = f"{arguments.output}@@{PARTITION_START_LBA * SECTOR_SIZE}"
    created_directories = set()

    for mapping in arguments.add:
        local_path, _, image_path = mapping.partition(":")
        if not image_path:
            print(f"malformed --add value: {mapping}", file=sys.stderr)
            return 1
        if not os.path.exists(local_path):
            print(f"missing file: {local_path}", file=sys.stderr)
            return 1

        directory = os.path.dirname(image_path)
        parts = [part for part in directory.split("/") if part]
        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            if prefix not in created_directories:
                subprocess.run(["mmd", "-i", image_spec, f"::{prefix}"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                created_directories.add(prefix)

        run(["mcopy", "-D", "o", "-i", image_spec, local_path, f"::{image_path}"])

    return 0


if __name__ == "__main__":
    sys.exit(main())
