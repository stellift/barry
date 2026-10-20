import argparse
import re
import subprocess
import sys

FUNCTION_TYPES = {"t", "T"}
HEADER = '#include "kernel/symbols.hpp"\n\nnamespace barry {\n\n'
FOOTER = "\n}\n"
SECTION = '__attribute__((used, section(".ksyms")))'


def read_function_symbols(nm_binary, elf_path):
    output = subprocess.run(
        [nm_binary, "--defined-only", "--demangle", "--print-size", "--no-sort", elf_path],
        check=True, capture_output=True, text=True,
    ).stdout

    symbols = []
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)?\s*([a-zA-Z])\s+(.*)$", line)
        if match is None:
            continue
        address_text, size_text, symbol_type, name = match.groups()
        if symbol_type not in FUNCTION_TYPES:
            continue
        symbols.append((int(address_text, 16), int(size_text or "0", 16), name.strip()))

    symbols.sort(key=lambda entry: entry[0])
    return symbols


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def render(symbols):
    names_blob = []
    offsets = []
    cursor = 0
    for _, _, name in symbols:
        offsets.append(cursor)
        names_blob.append(name)
        cursor += len(name.encode("utf-8")) + 1

    lines = [HEADER]
    lines.append(f"{SECTION} char const KERNEL_SYMBOL_NAMES[] =\n")
    if names_blob:
        for name in names_blob:
            lines.append(f'    "{escape(name)}\\0"\n')
    else:
        lines.append('    "\\0"\n')
    lines.append("    ;\n\n")

    lines.append(f"{SECTION} KernelSymbol const KERNEL_SYMBOLS[] = {{\n")
    if symbols:
        for (address, size, _), offset in zip(symbols, offsets):
            lines.append(f"    {{ 0x{address:x}ULL, {size}U, {offset}U }},\n")
    else:
        lines.append("    { 0ULL, 0U, 0U },\n")
    lines.append("};\n\n")

    lines.append(f"{SECTION} u32 const KERNEL_SYMBOL_COUNT = {len(symbols)}U;")
    lines.append(FOOTER)
    return "".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--elf")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--verify-against")
    arguments = parser.parse_args()

    symbols = read_function_symbols(arguments.nm, arguments.elf) if arguments.elf else []

    if arguments.verify_against:
        previous = read_function_symbols(arguments.nm, arguments.verify_against)
        if [(a, n) for a, _, n in previous] != [(a, n) for a, _, n in symbols]:
            print("symbol addresses moved between link passes", file=sys.stderr)
            return 1

    with open(arguments.output, "w") as output:
        output.write(render(symbols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
