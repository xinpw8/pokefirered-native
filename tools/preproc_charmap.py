#!/usr/bin/env python3
"""
preproc_charmap.py - Build-time _() string preprocessor for native builds.

On GBA, the assembler preprocessor converts _("text") from ASCII to Pokemon
character encoding via charmap.txt. On native, the C compiler sees plain ASCII.

This script preprocesses C source files, replacing _("...") with byte arrays
containing the Pokemon encoding. It acts as the native equivalent of the GBA
assembler's charmap processing.

Usage: python3 preproc_charmap.py <charmap.txt> <input.c> <output.c>
"""

import sys
import re
import os

def parse_charmap(path):
    """Parse charmap.txt into char_mapping and token_mapping.

    char_mapping: quoted entries (character literals, escape sequences) — used
        for converting characters inside string literals.
    token_mapping: named tokens (EOS, COLOR, HIGHLIGHT, etc.) — used for
        {TOKEN} and {TOKEN HH} syntax inside __() strings.
    """
    char_mapping = {}   # str -> list of ints
    token_mapping = {}  # str -> list of ints
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("@"):
                continue
            parts = line.split("@")[0].strip()
            if "=" not in parts:
                continue
            lhs, rhs = parts.split("=", 1)
            lhs = lhs.strip()
            rhs = rhs.strip()

            # Parse RHS as space-separated hex bytes
            try:
                vals = [int(x, 16) for x in rhs.split()]
            except ValueError:
                continue

            if lhs.startswith("'") and lhs.endswith("'"):
                inner = lhs[1:-1]
                if inner == "\\'":
                    char_mapping["'"] = vals
                elif inner.startswith("\\"):
                    char_mapping[inner] = vals
                else:
                    char_mapping[inner] = vals
            else:
                # Named token (EOS, COLOR, HIGHLIGHT, etc.)
                token_mapping[lhs] = vals

    return char_mapping, token_mapping


def convert_string(s, char_mapping, token_mapping):
    """Convert a string using the charmap, returning a list of byte values.

    The input is the raw content between quotes in _("...") or __("..."),
    as it appears in the C source.

    Handles:
    - Backslash escapes: \\n, \\l, \\p
    - {TOKEN} syntax: {COLOR}, {HIGHLIGHT}, etc. — emits token bytes
    - {TOKEN HH} syntax: {COLOR 01} — emits token bytes + hex parameter
    - {TOKEN HH HH HH} syntax: {COLOR_HIGHLIGHT_SHADOW 01 02 03}
    - Regular characters via char_mapping
    """
    result = []
    i = 0
    while i < len(s):
        # Handle {TOKEN} and {TOKEN HH ...} syntax
        if s[i] == '{':
            end = s.find('}', i)
            if end != -1:
                content = s[i+1:end].strip()
                parts = content.split()
                token_name = parts[0]
                if token_name in token_mapping:
                    result.extend(token_mapping[token_name])
                    # Remaining parts may be named charmap values (e.g. WHITE)
                    # or raw hex parameters (e.g. 01 / 0x01).
                    for p in parts[1:]:
                        if p in token_mapping:
                            result.extend(token_mapping[p])
                            continue
                        try:
                            result.append(int(p, 16))
                        except ValueError:
                            pass
                    i = end + 1
                    continue
            # Unrecognized {token} — fall through to character processing
            # Skip the '{' character
            result.append(0x00)
            i += 1
            continue

        # Try escape sequences first (backslash + next char)
        if s[i] == '\\' and i + 1 < len(s):
            esc = s[i:i+2]  # e.g., \n, \p, \l
            if esc in char_mapping:
                result.extend(char_mapping[esc])
                i += 2
                continue
            # Unknown escape - skip backslash, process next char
            i += 1
            continue

        # Try multi-byte UTF-8 characters (4, 3, 2, then 1 char)
        found = False
        for length in (4, 3, 2, 1):
            if i + length <= len(s):
                substr = s[i:i+length]
                if substr in char_mapping:
                    result.extend(char_mapping[substr])
                    i += length
                    found = True
                    break
        if found:
            continue

        # Character not in charmap - use 0x00 as fallback
        result.append(0x00)
        i += 1

    return result


def bytes_to_c_array(byte_list, terminate):
    """Convert a list of byte values to a C byte array initializer."""
    if terminate:
        byte_list = byte_list + [0xFF]
    parts = ["0x%02X" % b for b in byte_list]
    return "{" + ", ".join(parts) + "}"


def process_source(source, char_mapping, token_mapping):
    """Process a C source file, replacing _()/__() string macros with byte arrays."""
    # Match one or more adjacent C string literals inside _()/__(), including
    # multi-line forms such as:
    #   _("foo\n"
    #     "bar")
    literal_block = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
    pattern = re.compile(rf'(_{{1,2}})\(\s*{literal_block}\)', re.DOTALL)

    def replacer(m):
        prefix = m.group(1)
        literal_text = m.group(2)
        raw_parts = re.findall(r'"((?:[^"\\]|\\.)*)"', literal_text, re.DOTALL)
        raw_str = "".join(raw_parts)
        byte_vals = convert_string(raw_str, char_mapping, token_mapping)
        return bytes_to_c_array(byte_vals, terminate=(prefix == "_"))

    return pattern.sub(replacer, source)


def main():
    if len(sys.argv) != 4:
        print("Usage: %s <charmap.txt> <input.c> <output.c>" % sys.argv[0], file=sys.stderr)
        sys.exit(1)

    charmap_path = sys.argv[1]
    input_path = sys.argv[2]
    output_path = sys.argv[3]

    char_mapping, token_mapping = parse_charmap(charmap_path)

    with open(input_path, 'r', encoding='utf-8') as f:
        source = f.read()

    result = process_source(source, char_mapping, token_mapping)

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(result)


if __name__ == "__main__":
    main()
