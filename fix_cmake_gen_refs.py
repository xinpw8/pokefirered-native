#!/usr/bin/env python3
"""
Fix CMakeLists.txt to use gen versions of source files instead of originals.

For each ${POKEFIRERED_SOURCE_DIR}/src/XXX.c reference inside add_library() blocks,
checks if build/gen/XXX.c exists on disk, and if so replaces the reference with
${PFR_GEN_DIR}/XXX.c.

Does NOT touch references inside:
  - set(PFR_BOOT_ASSET_TUS ...)
  - set(PFR_PREPROCESSED_SOURCES ...)
  - Any other non-add_library context
"""

import os
import re
import sys

CMAKE_FILE = "/home/spark-advantage/pokefirered-native/CMakeLists.txt"
GEN_DIR = "/home/spark-advantage/pokefirered-native/build/gen"

def main():
    with open(CMAKE_FILE, "r") as f:
        lines = f.readlines()

    # Identify which lines are inside add_library() blocks.
    # Track paren depth after seeing "add_library(".
    # Handle nested parens (e.g., generator expressions).

    in_add_library = False
    paren_depth = 0
    add_library_lines = set()

    # Also identify lines inside set(PFR_BOOT_ASSET_TUS ...) and
    # set(PFR_PREPROCESSED_SOURCES ...) to exclude them.
    in_excluded_set = False
    excluded_set_depth = 0
    excluded_lines = set()

    for i, line in enumerate(lines):
        stripped = line.strip()

        # Check for start of excluded set() blocks
        if not in_excluded_set and not in_add_library:
            if re.match(r'^set\s*\(\s*PFR_BOOT_ASSET_TUS\b', stripped) or \
               re.match(r'^set\s*\(\s*PFR_PREPROCESSED_SOURCES\b', stripped):
                in_excluded_set = True
                excluded_set_depth = 0
                for ch in line:
                    if ch == '(':
                        excluded_set_depth += 1
                    elif ch == ')':
                        excluded_set_depth -= 1
                excluded_lines.add(i)
                if excluded_set_depth <= 0:
                    in_excluded_set = False
                continue

        if in_excluded_set:
            excluded_lines.add(i)
            for ch in line:
                if ch == '(':
                    excluded_set_depth += 1
                elif ch == ')':
                    excluded_set_depth -= 1
            if excluded_set_depth <= 0:
                in_excluded_set = False
            continue

        # Check for start of add_library blocks
        if not in_add_library:
            if re.match(r'^add_library\s*\(', stripped):
                in_add_library = True
                paren_depth = 0
                for ch in line:
                    if ch == '(':
                        paren_depth += 1
                    elif ch == ')':
                        paren_depth -= 1
                add_library_lines.add(i)
                if paren_depth <= 0:
                    in_add_library = False
                continue

        if in_add_library:
            add_library_lines.add(i)
            for ch in line:
                if ch == '(':
                    paren_depth += 1
                elif ch == ')':
                    paren_depth -= 1
            if paren_depth <= 0:
                in_add_library = False

    # Process: for lines in add_library blocks (and NOT in excluded blocks),
    # replace ${POKEFIRERED_SOURCE_DIR}/src/XXX.c with ${PFR_GEN_DIR}/XXX.c
    # if build/gen/XXX.c exists on disk.

    pattern = re.compile(r'\$\{POKEFIRERED_SOURCE_DIR\}/src/([A-Za-z0-9_]+\.c)')
    changed_count = 0
    changed_samples = []

    for i in sorted(add_library_lines):
        if i in excluded_lines:
            continue

        line = lines[i]
        m = pattern.search(line)
        if m:
            filename = m.group(1)
            gen_path = os.path.join(GEN_DIR, filename)
            if os.path.isfile(gen_path):
                old_ref = '${POKEFIRERED_SOURCE_DIR}/src/' + filename
                new_ref = '${PFR_GEN_DIR}/' + filename
                new_line = line.replace(old_ref, new_ref)
                if new_line != line:
                    lines[i] = new_line
                    changed_count += 1
                    changed_samples.append((i + 1, new_line.strip()))

    # Write the modified file
    with open(CMAKE_FILE, "w") as f:
        f.writelines(lines)

    print(f"Total references changed: {changed_count}")
    print()
    print("Sample of changed lines (up to 10):")
    for lineno, content in changed_samples[:10]:
        print(f"  L{lineno}: {content}")

    if changed_count > 10:
        print(f"  ... and {changed_count - 10} more")

    return 0

if __name__ == "__main__":
    sys.exit(main())
