import argparse
import os
import sys

MAGIC = b"070701"
HEADER_FIELDS = 13
TRAILER_NAME = b"TRAILER!!!"
MODE_DIRECTORY = 0o040755
MODE_EXECUTABLE = 0o100755
RECORD_ALIGNMENT = 4


def hex_field(value):
    return b"%08X" % (value & 0xFFFFFFFF)


def pad(data, alignment=RECORD_ALIGNMENT):
    remainder = len(data) % alignment
    return data if remainder == 0 else data + b"\0" * (alignment - remainder)


def record(inode, mode, name, contents):
    encoded_name = name.encode("utf-8") + b"\0"
    header = MAGIC
    header += hex_field(inode)
    header += hex_field(mode)
    header += hex_field(0)
    header += hex_field(0)
    header += hex_field(1)
    header += hex_field(0)
    header += hex_field(len(contents))
    header += hex_field(0)
    header += hex_field(0)
    header += hex_field(0)
    header += hex_field(0)
    header += hex_field(len(encoded_name))
    header += hex_field(0)
    return pad(header + encoded_name) + pad(contents)


def directories_for(paths):
    seen = []
    for path in paths:
        parts = path.split("/")[:-1]
        for depth in range(1, len(parts) + 1):
            prefix = "/".join(parts[:depth])
            if prefix and prefix not in seen:
                seen.append(prefix)
    return seen


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--add", action="append", default=[], metavar="LOCAL:ARCHIVE")
    arguments = parser.parse_args()

    entries = []
    for mapping in arguments.add:
        local_path, _, archive_path = mapping.partition(":")
        if not archive_path:
            print(f"malformed --add value: {mapping}", file=sys.stderr)
            return 1
        if not os.path.isfile(local_path):
            print(f"missing file: {local_path}", file=sys.stderr)
            return 1
        entries.append((local_path, archive_path.lstrip("/")))

    archive = bytearray()
    inode = 1

    for directory in directories_for([path for _, path in entries]):
        archive += record(inode, MODE_DIRECTORY, directory, b"")
        inode += 1

    for local_path, archive_path in entries:
        with open(local_path, "rb") as source:
            archive += record(inode, MODE_EXECUTABLE, archive_path, source.read())
        inode += 1

    archive += record(0, 0, TRAILER_NAME.decode(), b"")

    with open(arguments.output, "wb") as output:
        output.write(bytes(archive))
    return 0


if __name__ == "__main__":
    sys.exit(main())
