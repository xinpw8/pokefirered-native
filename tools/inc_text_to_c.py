#!/usr/bin/env python3
"""
Convert pokefirered .inc text data files to C source.

Reads the charmap.txt to build the Pokemon character encoding map,
then parses .inc files containing label::/.string directives and
emits C source with proper const u8[] arrays.

Usage:
    python3 inc_text_to_c.py <charmap> <output.c> <input1.inc> [input2.inc ...]
"""

import re
import sys
from pathlib import Path


def parse_charmap(path):
    """Parse charmap.txt into a dict mapping names/chars to byte sequences.

    Only keeps the FIRST mapping for each key (English section comes first
    in the charmap; Japanese characters reuse the same byte values but must
    not overwrite the English entries).
    """
    charmap = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("@")[0].strip()  # strip comments
            if not line:
                continue
            # Match: KEY = HEX_BYTES (split on ' = ' to handle '=' literal)
            m = re.match(r"^(.+?)\s+=\s+([0-9A-Fa-f][0-9A-Fa-f\s]*)$", line)
            if not m:
                continue
            lhs = m.group(1).strip()
            rhs = m.group(2).strip()

            # Parse the byte values
            try:
                byte_vals = [int(b, 16) for b in rhs.split()]
            except ValueError:
                continue

            if lhs.startswith("'") and lhs.endswith("'"):
                # Character literal: extract the char(s)
                inner = lhs[1:-1]
                if inner == "\\'":
                    inner = "'"
                # Don't overwrite existing entries (English first)
                if inner not in charmap:
                    charmap[inner] = byte_vals
            else:
                # Named constant (PLAYER, RIVAL, etc.)
                if lhs not in charmap:
                    charmap[lhs] = byte_vals

    return charmap


def encode_string(text, charmap):
    """Encode a .string text using the charmap. Returns list of ints."""
    result = []
    i = 0
    while i < len(text):
        # Check for escape sequences (\n, \p, \l)
        if text[i] == "\\" and i + 1 < len(text):
            esc = text[i : i + 2]
            if esc in charmap:
                result.extend(charmap[esc])
                i += 2
                continue
            # Unknown escape — skip the backslash
            i += 1
            continue

        # Check for {NAME} or {CMD ARG} placeholders
        if text[i] == "{":
            end = text.find("}", i)
            if end != -1:
                inner = text[i + 1 : end]

                # Try exact match first (e.g. PLAYER, FONT_MALE)
                if inner in charmap:
                    result.extend(charmap[inner])
                    i = end + 1
                    continue

                # Try space-separated tokens: {COLOR BLUE} → COLOR + BLUE
                tokens = inner.split()
                all_found = True
                token_bytes = []
                for token in tokens:
                    if token in charmap:
                        token_bytes.extend(charmap[token])
                    else:
                        all_found = False
                        break
                if all_found and token_bytes:
                    result.extend(token_bytes)
                    i = end + 1
                    continue

                # Unknown placeholder — warn and skip the braces
                print(f"  Warning: unmapped placeholder {{{inner}}}", file=sys.stderr)
                i = end + 1
                continue
            # No closing brace
            i += 1
            continue

        # Single character lookup
        ch = text[i]
        if ch in charmap:
            result.extend(charmap[ch])
        elif ch == "$":
            # EOS terminator
            result.append(0xFF)
        else:
            print(f"  Warning: unmapped character {ch!r} (U+{ord(ch):04X})", file=sys.stderr)
            result.append(0x00)
        i += 1

    return result


def parse_inc_file(path, charmap):
    """
    Parse a .inc file, extracting (label, encoded_bytes) pairs.
    Handles multi-line .string continuations for the same label.
    """
    entries = []
    current_label = None
    current_bytes = []

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip()

            # Check for label (name followed by :: at start of line)
            label_match = re.match(r"^(\w+)::", line)
            if label_match:
                # Save previous entry
                if current_label and current_bytes:
                    entries.append((current_label, current_bytes))
                current_label = label_match.group(1)
                current_bytes = []
                continue

            # Check for .string directive
            string_match = re.match(r'\s*\.string\s+"(.*)"', line)
            if string_match and current_label:
                text = string_match.group(1)
                current_bytes.extend(encode_string(text, charmap))
                continue

    # Don't forget the last entry
    if current_label and current_bytes:
        entries.append((current_label, current_bytes))

    return entries


def format_c_array(label, byte_data, line_width=12):
    """Format a single entry as a C const u8 array."""
    lines = []
    lines.append(f"const u8 {label}[] = {{")
    for i in range(0, len(byte_data), line_width):
        chunk = byte_data[i : i + line_width]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        comma = "," if i + line_width < len(byte_data) else ""
        lines.append(f"    {hex_vals}{comma}")
    lines.append("};")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <charmap> <output.c> <input1.inc> [input2.inc ...]",
              file=sys.stderr)
        sys.exit(1)

    charmap_path = sys.argv[1]
    output_path = sys.argv[2]
    input_paths = sys.argv[3:]

    charmap = parse_charmap(charmap_path)

    all_entries = []
    for inp in input_paths:
        entries = parse_inc_file(inp, charmap)
        print(f"  {Path(inp).name}: {len(entries)} text entries", file=sys.stderr)
        all_entries.extend(entries)

    # Generate C source
    with open(output_path, "w", encoding="utf-8") as out:
        out.write("/*\n")
        out.write(" * Auto-generated by tools/inc_text_to_c.py\n")
        out.write(" * Source .inc files:\n")
        for inp in input_paths:
            out.write(f" *   {Path(inp).name}\n")
        out.write(" *\n")
        out.write(" * DO NOT EDIT — regenerate with the command shown below.\n")
        out.write(" */\n\n")
        out.write('#include "global.h"\n\n')

        for label, byte_data in all_entries:
            out.write(format_c_array(label, byte_data))
            out.write("\n\n")

    print(f"Generated {output_path}: {len(all_entries)} symbols", file=sys.stderr)


if __name__ == "__main__":
    main()
