#!/usr/bin/env python3
"""gen_game_ctx.py -- Build-time code generator for the GameCtx struct.

Scans all .c files under the pokefirered-native codebase for EWRAM_DATA
variable declarations and produces:

  game_ctx.h          -- typedef struct GameCtx { ... };
  game_ctx_macros.h   -- #define gFoo (g_ctx->gFoo) for each non-static global

With --transform, also produces:
  build/gen/<name>.c  -- transformed .c files with EWRAM_DATA removed and
                         global references rewritten to g_ctx-> access
  game_ctx_stubs.c    -- stub definitions for extern declarations in headers

Usage:
  python3 tools/gen_game_ctx.py \\
      --src-dir third_party/pokefirered/src \\
      --host-src-dir src \\
      --out-dir build/generated_include \\
      [--gen-dir build/gen] \\
      [--transform] \\
      [--dry-run]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------

@dataclass
class GlobalVar:
    """One parsed EWRAM_DATA global variable."""
    is_static: bool
    type_str: str           # full C type for a plain variable (e.g. "u8", "struct Foo *")
    name: str               # variable name (e.g. "gSomeVar")
    array_dims: str         # e.g. "[256]" or "[MAX_BATTLERS_COUNT][0x200]" or ""
    is_func_ptr: bool       # true for void (*name)(void) style
    func_ptr_decl: str      # full field declaration for func ptrs: "void (*name)(void)"
    is_ptr_to_array: bool   # true for u16 (*name)[0x400] style
    ptr_array_decl: str     # full field declaration for ptr-to-array
    source_file: str        # basename of the .c file (e.g. "sprite.c")
    line_no: int
    is_anon_struct: bool    # anonymous struct/union defined inline
    anon_struct_body: str   # the full struct { ... } body including braces
    is_ptr: bool            # whether the var itself is a pointer to the anon struct
    is_file_scope: bool     # True when declared at file scope, False for function-local static storage
    original_name: str = "" # name before dedup renaming (set during dedup)
    raw_line: str = ""      # the original source line(s) for EWRAM_DATA definition


@dataclass
class SectionMacroUse:
    """One use of a storage-section macro in upstream source."""
    macro: str
    source_file: str
    line_no: int
    is_file_scope: bool
    raw_line: str


# ---------------------------------------------------------------------------
# Regex patterns
# ---------------------------------------------------------------------------

# Matches a single-line EWRAM_DATA declaration.
# Handles all ordering variants:
#   EWRAM_DATA static u8 x = 0;
#   static EWRAM_DATA u8 x = 0;
#   EWRAM_DATA u8 x = 0;
#   ALIGNED(4) EWRAM_DATA u8 x = 0;
#   static ALIGNED(4) EWRAM_DATA u8 x = 0;
RE_EWRAM_LINE = re.compile(
    r'^'
    r'(?P<pre_static>static\s+)?'
    r'(?P<pre_aligned>ALIGNED\(\d+\)\s+)?'
    r'(?:EWRAM_DATA)\s+'
    r'(?P<post_static>static\s+)?'
    r'(?P<rest>.+?)\s*$',
    re.MULTILINE,
)

# Pattern for the closing-brace anonymous struct form:
#   } static EWRAM_DATA sFrenzyPlantRootData = {0};
#   } static EWRAM_DATA * sTradeAnim = NULL;
RE_CLOSING_BRACE = re.compile(
    r'^\}\s*'
    r'(?P<pre_static>static\s+)?'
    r'(?:EWRAM_DATA)\s+'
    r'(?P<rest>.+?)\s*$',
    re.MULTILINE,
)


# ---------------------------------------------------------------------------
# Multi-line anonymous struct collection
# ---------------------------------------------------------------------------

def find_anon_struct_start(lines: List[str], decl_line_idx: int) -> int:
    """Given that lines[decl_line_idx] contains a line like:
        static EWRAM_DATA struct {
    or  EWRAM_DATA union
    find the opening brace line and return its index.
    """
    line = lines[decl_line_idx]
    if '{' in line:
        return decl_line_idx
    if decl_line_idx + 1 < len(lines) and '{' in lines[decl_line_idx + 1]:
        return decl_line_idx + 1
    return decl_line_idx


def collect_anon_struct_body(lines: List[str], start_idx: int) -> Tuple[str, int]:
    """Starting from the line containing '{', collect lines until the
    matching '}' and the variable name + initializer on the closing line.
    Returns (body_text, end_line_index).
    """
    depth = 0
    collected = []
    for i in range(start_idx, len(lines)):
        line = lines[i]
        collected.append(line)
        depth += line.count('{') - line.count('}')
        if depth <= 0:
            return '\n'.join(collected), i
    return '\n'.join(collected), len(lines) - 1


def extract_clean_struct_body(raw_body: str, struct_or_union: str) -> str:
    """Given the raw collected text for an anonymous struct/union,
    extract the 'struct { ... }' or 'union { ... }' portion,
    stripping variable declarations, initializers, qualifiers.
    """
    first_brace = raw_body.find('{')
    if first_brace < 0:
        return raw_body

    depth = 0
    last_brace = -1
    for i, ch in enumerate(raw_body):
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                last_brace = i
                break

    if last_brace < 0:
        last_brace = raw_body.rfind('}')
    if last_brace < 0:
        return raw_body

    interior = raw_body[first_brace + 1:last_brace]
    interior_lines = interior.splitlines()
    cleaned_lines = []
    for line in interior_lines:
        stripped = line.strip()
        if stripped:
            cleaned_lines.append(f'        {stripped}')

    interior_clean = '\n'.join(cleaned_lines)
    return f'{struct_or_union} {{\n{interior_clean}\n    }}'


def parse_anon_closing_line(closing_line: str) -> Tuple[str, str, bool]:
    """Parse the closing line of an anonymous struct, e.g.:
        } sTilesetDMA3TransferBuffer[20] = {0};
        } *sMultiMove = NULL;
        } sLinkErrorBuffer = {};
    Returns (var_name, array_dims, is_pointer).
    """
    m = re.match(r'^\}\s*(.+?)\s*=\s*.*$', closing_line)
    if not m:
        m = re.match(r'^\}\s*(.+?)\s*;', closing_line)
    if not m:
        return ('__unknown__', '', False)

    rest = m.group(1).strip()
    is_ptr = False
    if rest.startswith('*'):
        is_ptr = True
        rest = rest.lstrip('*').strip()

    m2 = re.match(r'^(\w+)((?:\[.*?\])*)$', rest)
    if m2:
        return (m2.group(1), m2.group(2), is_ptr)
    m3 = re.match(r'^(\w+)', rest)
    if m3:
        return (m3.group(1), '', is_ptr)
    return ('__unknown__', '', is_ptr)


# ---------------------------------------------------------------------------
# Single-line declaration parser
# ---------------------------------------------------------------------------

def parse_rest(rest: str, source_file: str, line_no: int,
               is_static: bool,
               is_file_scope: bool,
               raw_line: str) -> Optional[GlobalVar]:
    """Parse the 'rest' portion after stripping EWRAM_DATA/static/ALIGNED.
    This is the type + name + array dims + initializer.
    """
    # Strip trailing ; and everything after =
    rest = rest.rstrip(';').strip()
    # Remove inline comments
    comment_idx = rest.find('//')
    if comment_idx >= 0:
        rest = rest[:comment_idx].strip()
    # Remove initializer
    init_match = re.match(r'^(.+?)\s*=\s*.*$', rest, re.DOTALL)
    if init_match:
        rest = init_match.group(1).strip()

    # Skip truly const globals (but NOT "const u8 *ptr")
    tokens = rest.split()
    if tokens and tokens[0] == 'const':
        if '*' not in rest and '(' not in rest:
            return None  # truly const global, skip

    # ---------------------------------------------------------------
    # Case 1: Function pointer:  void (*name)(args)
    # ---------------------------------------------------------------
    fp_match = re.match(
        r'^(.+?)\s*\(\s*\*\s*(\w+)\s*\)\s*(\(.*\))$', rest
    )
    if fp_match:
        ret_type = fp_match.group(1).strip()
        name = fp_match.group(2)
        params = fp_match.group(3)
        func_ptr_decl = f'{ret_type} (*{name}){params}'
        return GlobalVar(
            is_static=is_static,
            type_str=f'{ret_type} (*){params}',
            name=name, array_dims='',
            is_func_ptr=True, func_ptr_decl=func_ptr_decl,
            is_ptr_to_array=False, ptr_array_decl='',
            source_file=source_file, line_no=line_no,
            is_anon_struct=False, anon_struct_body='', is_ptr=False,
            is_file_scope=is_file_scope,
            raw_line=raw_line,
        )

    # ---------------------------------------------------------------
    # Case 2: Pointer-to-array:  u16 (*name)[0x400]
    # ---------------------------------------------------------------
    pa_match = re.match(
        r'^(.+?)\s*\(\s*\*\s*(\w+)\s*\)\s*(\[.*\])$', rest
    )
    if pa_match:
        base_type = pa_match.group(1).strip()
        name = pa_match.group(2)
        arr_dim = pa_match.group(3)
        ptr_array_decl = f'{base_type} (*{name}){arr_dim}'
        return GlobalVar(
            is_static=is_static,
            type_str=f'{base_type} (*){arr_dim}',
            name=name, array_dims='',
            is_func_ptr=False, func_ptr_decl='',
            is_ptr_to_array=True, ptr_array_decl=ptr_array_decl,
            source_file=source_file, line_no=line_no,
            is_anon_struct=False, anon_struct_body='', is_ptr=False,
            is_file_scope=is_file_scope,
            raw_line=raw_line,
        )

    # ---------------------------------------------------------------
    # Case 3: Parenthesized pointer with array subscript:
    #   const u8 (*sMovementScripts[OBJECT_EVENTS_COUNT])
    # ---------------------------------------------------------------
    parr_match = re.match(
        r'^(.+?)\s*\(\s*\*?\s*(\w+)\s*(\[.*?\])\s*\)$', rest
    )
    if parr_match:
        base_type = parr_match.group(1).strip()
        name = parr_match.group(2)
        arr_dim = parr_match.group(3)
        return GlobalVar(
            is_static=is_static,
            type_str=f'{base_type} *',
            name=name, array_dims=arr_dim,
            is_func_ptr=False, func_ptr_decl='',
            is_ptr_to_array=False, ptr_array_decl='',
            source_file=source_file, line_no=line_no,
            is_anon_struct=False, anon_struct_body='', is_ptr=False,
            is_file_scope=is_file_scope,
            raw_line=raw_line,
        )

    # ---------------------------------------------------------------
    # Case 4: Normal variable
    # ---------------------------------------------------------------
    arr_match = re.match(r'^(.*?)(\s*(?:\[.*?\])+)$', rest)
    if arr_match:
        type_and_name = arr_match.group(1).strip()
        array_dims = arr_match.group(2).strip()
    else:
        type_and_name = rest.strip()
        array_dims = ''

    parts = type_and_name.rsplit(None, 1)
    if len(parts) == 2:
        type_str = parts[0].strip()
        name = parts[1].strip()
        if name.startswith('*'):
            stars = ''
            while name.startswith('*'):
                stars += '*'
                name = name[1:]
            name = name.strip()
            type_str = type_str + ' ' + stars
    elif len(parts) == 1:
        name = parts[0]
        type_str = 'void'
    else:
        return None

    type_str = re.sub(r'\s*\*\s*', ' *', type_str).strip()
    type_str = re.sub(r'\s+', ' ', type_str)

    if not re.match(r'^[a-zA-Z_]\w*$', name):
        return None

    return GlobalVar(
        is_static=is_static,
        type_str=type_str,
        name=name, array_dims=array_dims,
        is_func_ptr=False, func_ptr_decl='',
        is_ptr_to_array=False, ptr_array_decl='',
        source_file=source_file, line_no=line_no,
        is_anon_struct=False, anon_struct_body='', is_ptr=False,
        is_file_scope=is_file_scope,
        raw_line=raw_line,
    )


# ---------------------------------------------------------------------------
# File scanner
# ---------------------------------------------------------------------------

SECTION_MACROS = ('EWRAM_DATA', 'COMMON_DATA', 'IWRAM_DATA', 'IWRAM_CODE')


def compute_file_scope_flags(text: str) -> List[bool]:
    """Return a per-line flag indicating whether each line starts at file scope.

    This uses a lightweight lexer that tracks braces while ignoring comments
    and string/char literals well enough for upstream source inventorying.
    """
    flags: List[bool] = []
    depth = 0
    in_block_comment = False

    for line in text.splitlines():
        flags.append(depth == 0 and not in_block_comment)

        i = 0
        while i < len(line):
            if in_block_comment:
                end = line.find('*/', i)
                if end < 0:
                    i = len(line)
                    continue
                in_block_comment = False
                i = end + 2
                continue

            two = line[i:i + 2]
            ch = line[i]

            if two == '//':
                break
            if two == '/*':
                in_block_comment = True
                i += 2
                continue
            if ch in ('"', "'"):
                quote = ch
                i += 1
                while i < len(line):
                    if line[i] == '\\':
                        i += 2
                        continue
                    if line[i] == quote:
                        i += 1
                        break
                    i += 1
                continue
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth = max(0, depth - 1)
            i += 1

    return flags


def collect_source_files(src_dir: Path, host_src_dir: Path) -> List[Path]:
    """Return the upstream/host source files relevant to GameCtx scanning."""
    source_files: List[Path] = []

    if src_dir.is_dir():
        source_files.extend(sorted(src_dir.glob('*.c')))
    else:
        print(f'WARNING: directory not found: {src_dir}', file=sys.stderr)

    stubs_file = host_src_dir / 'upstream_stubs.c'
    if stubs_file.is_file():
        source_files.append(stubs_file)
    else:
        print(f'WARNING: file not found: {stubs_file}', file=sys.stderr)

    return source_files

def scan_file(filepath: Path) -> List[GlobalVar]:
    """Scan a single .c file for EWRAM_DATA declarations."""
    results: List[GlobalVar] = []
    source_file = filepath.name

    try:
        text = filepath.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return results

    lines = text.splitlines()
    file_scope_flags = compute_file_scope_flags(text)
    handled_lines: Set[int] = set()

    # ---------------------------------------------------------------
    # Pass 1: Multi-line anonymous struct/union declarations
    # ---------------------------------------------------------------
    for i, line in enumerate(lines):
        if i in handled_lines:
            continue

        m = re.match(
            r'^(?:static\s+)?'
            r'(?:ALIGNED\(\d+\)\s+)?'
            r'(?:EWRAM_DATA)\s+'
            r'(?:static\s+)?'
            r'(?:struct|union)\s*\{?\s*$',
            line.strip()
        )
        if not m:
            m = re.match(
                r'^(?:static\s+)?'
                r'(?:ALIGNED\(\d+\)\s+)?'
                r'(?:EWRAM_DATA)\s+'
                r'(?:static\s+)?'
                r'(?:struct|union)\s*\{',
                line.strip()
            )
            if m:
                after = re.search(r'(?:EWRAM_DATA)\s+(?:static\s+)?(?:struct|union)\s*(\{|\w)', line)
                if after and after.group(1) != '{':
                    m = None

        if not m:
            continue

        is_static = bool(re.match(r'.*\bstatic\b', line.split('EWRAM_DATA')[0])) or \
                     bool(re.search(r'EWRAM_DATA\s+static\b', line))

        brace_start = find_anon_struct_start(lines, i)
        body_text, end_idx = collect_anon_struct_body(lines, brace_start)

        for j in range(i, end_idx + 1):
            handled_lines.add(j)

        closing_line = lines[end_idx].strip()
        var_name, arr_dims, is_ptr = parse_anon_closing_line(closing_line)

        if var_name == '__unknown__':
            continue

        struct_or_union = 'struct'
        su_match = re.search(r'\b(struct|union)\b', lines[i])
        if su_match:
            struct_or_union = su_match.group(1)

        full_body = extract_clean_struct_body(body_text, struct_or_union)

        results.append(GlobalVar(
            is_static=is_static,
            type_str='',
            name=var_name, array_dims=arr_dims,
            is_func_ptr=False, func_ptr_decl='',
            is_ptr_to_array=False, ptr_array_decl='',
            source_file=source_file, line_no=i + 1,
            is_anon_struct=True, anon_struct_body=full_body,
            is_ptr=is_ptr,
            is_file_scope=file_scope_flags[i],
            raw_line='\n'.join(lines[i:end_idx + 1]).strip(),
        ))

    # ---------------------------------------------------------------
    # Pass 1b: Closing-brace anonymous struct forms
    # ---------------------------------------------------------------
    for i, line in enumerate(lines):
        if i in handled_lines:
            continue

        m = RE_CLOSING_BRACE.match(line.strip())
        if not m:
            continue

        is_static = bool(m.group('pre_static'))
        rest = m.group('rest').strip()

        is_ptr = False
        if rest.startswith('*'):
            is_ptr = True
            rest = rest.lstrip('*').strip()

        name_match = re.match(r'^(\w+)', rest)
        if not name_match:
            continue
        var_name = name_match.group(1)

        struct_start = None
        struct_or_union = 'struct'
        depth = 1
        for j in range(i - 1, -1, -1):
            depth += lines[j].count('}') - lines[j].count('{')
            if depth <= 0:
                struct_start = j
                for k in range(j, max(j - 3, -1), -1):
                    if 'struct' in lines[k] or 'union' in lines[k]:
                        su_m = re.search(r'\b(struct|union)\b', lines[k])
                        if su_m:
                            struct_or_union = su_m.group(1)
                            struct_start = k
                        break
                break

        if struct_start is None:
            continue

        for j in range(struct_start, i + 1):
            handled_lines.add(j)

        body_lines = lines[struct_start:i + 1]
        body_text = '\n'.join(body_lines)
        full_body = extract_clean_struct_body(body_text, struct_or_union)

        results.append(GlobalVar(
            is_static=is_static,
            type_str='',
            name=var_name, array_dims='',
            is_func_ptr=False, func_ptr_decl='',
            is_ptr_to_array=False, ptr_array_decl='',
            source_file=source_file, line_no=i + 1,
            is_anon_struct=True, anon_struct_body=full_body,
            is_ptr=is_ptr,
            is_file_scope=file_scope_flags[i],
            raw_line='\n'.join(lines[struct_start:i + 1]).strip(),
        ))

    # ---------------------------------------------------------------
    # Pass 2: Single-line declarations
    # ---------------------------------------------------------------
    for i, line in enumerate(lines):
        if i in handled_lines:
            continue

        stripped = line.strip()
        if 'EWRAM_DATA' not in stripped:
            continue

        m = RE_EWRAM_LINE.match(stripped)
        if not m:
            continue

        is_static = bool(m.group('pre_static') or m.group('post_static'))
        rest = m.group('rest')

        if re.match(r'^(?:struct|union)\s*\{', rest) or re.match(r'^(?:struct|union)\s*$', rest):
            continue

        gv = parse_rest(rest, source_file, i + 1, is_static,
                        file_scope_flags[i], line.strip())
        if gv is not None:
            results.append(gv)

    return results


def scan_all(src_dir: Path, host_src_dir: Path) -> List[GlobalVar]:
    """Scan all relevant .c files for EWRAM_DATA declarations."""
    all_globals: List[GlobalVar] = []

    for cfile in collect_source_files(src_dir, host_src_dir):
        all_globals.extend(scan_file(cfile))

    return all_globals


def scan_section_macro_usage(src_dir: Path,
                             host_src_dir: Path) -> Dict[str, List[SectionMacroUse]]:
    """Inventory storage-section macro usage across relevant source files."""
    usage: Dict[str, List[SectionMacroUse]] = {macro: [] for macro in SECTION_MACROS}

    for cfile in collect_source_files(src_dir, host_src_dir):
        try:
            text = cfile.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue

        file_scope_flags = compute_file_scope_flags(text)
        for idx, line in enumerate(text.splitlines()):
            stripped = line.strip()
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue
            for macro in SECTION_MACROS:
                if re.search(rf'\b{re.escape(macro)}\b', stripped):
                    usage[macro].append(SectionMacroUse(
                        macro=macro,
                        source_file=cfile.name,
                        line_no=idx + 1,
                        is_file_scope=file_scope_flags[idx],
                        raw_line=stripped,
                    ))

    return usage


# ---------------------------------------------------------------------------
# Deduplication and static name collision handling
# ---------------------------------------------------------------------------

def file_stem(path: str) -> str:
    """Get the stem of a filename, sanitized for C identifiers."""
    stem = Path(path).stem
    return re.sub(r'[^a-zA-Z0-9]', '_', stem)


def find_static_collisions(globals_list: List[GlobalVar]) -> Dict[str, List[str]]:
    """Return static names that collide across multiple source files."""
    static_names: Dict[str, Set[str]] = defaultdict(set)
    for gv in globals_list:
        if gv.is_static:
            static_names[gv.name].add(gv.source_file)

    return {
        name: sorted(files)
        for name, files in static_names.items()
        if len(files) > 1
    }


def find_duplicate_nonstatic(globals_list: List[GlobalVar]) -> Dict[str, List[str]]:
    """Return duplicate non-static globals keyed by the conflicting symbol name."""
    nonstatic_files: Dict[str, List[str]] = defaultdict(list)
    for gv in globals_list:
        if not gv.is_static:
            nonstatic_files[gv.name].append(gv.source_file)

    return {
        name: files
        for name, files in nonstatic_files.items()
        if len(files) > 1
    }


def deduplicate(globals_list: List[GlobalVar]) -> List[GlobalVar]:
    """Handle name collisions.

    For static variables with the same name in different files,
    disambiguate by appending __filename to the struct member name.

    For non-static globals, keep only the first occurrence and warn
    about duplicates.
    """
    collisions = find_static_collisions(globals_list)
    colliding_static_names = set(collisions)

    # Print collision report to stderr
    if colliding_static_names:
        print(f'\n=== Static name collisions ({len(colliding_static_names)} names) ===',
              file=sys.stderr)
        for name in sorted(colliding_static_names):
            print(f'  {name}: {", ".join(collisions[name])}', file=sys.stderr)
        print(file=sys.stderr)

    # Second pass: build deduplicated list
    seen_nonstatic: Dict[str, GlobalVar] = {}
    result: List[GlobalVar] = []
    duplicate_nonstatic = find_duplicate_nonstatic(globals_list)

    for gv in globals_list:
        if gv.is_static:
            if gv.name in colliding_static_names:
                gv.original_name = gv.name
                gv.name = f'{gv.name}__{file_stem(gv.source_file)}'
            else:
                gv.original_name = gv.name
            result.append(gv)
        else:
            if gv.name in seen_nonstatic:
                continue
            else:
                gv.original_name = gv.name
                seen_nonstatic[gv.name] = gv
                result.append(gv)

    if duplicate_nonstatic:
        print('\n=== Duplicate non-static globals (using first occurrence) ===',
              file=sys.stderr)
        for name, files in sorted(duplicate_nonstatic.items()):
            print(f'  {name}: {", ".join(files)}', file=sys.stderr)
        print(file=sys.stderr)

    return result


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

# Globals that must be excluded from the GameCtx struct because their names
# shadow local variables in the same file, making text replacement unsafe.
# Format: set of (source_file, original_name) tuples.
EXCLUDED_GLOBALS = {
    ('malloc.c', 'head'),       # shadows local 'struct MemBlock *head' in functions
    ('malloc.c', 'pos'),        # shadows local 'struct MemBlock *pos' in functions
    ('malloc.c', 'splitBlock'), # shadows local 'struct MemBlock *splitBlock'
}


# File-local constants that appear in array dimensions but aren't in headers.
# Map from constant name to literal value string.
# These will be substituted in field_declaration() for game_ctx.h.
FILE_LOCAL_CONSTANTS = {
    'WIN_COUNT': '4',              # quest_log.c enum (4 windows)
    'MAX_STARTMENU_ITEMS': '10',   # start_menu.c enum
    'SPR_COUNT': '9',              # item_menu_icons.c enum
    'NUM_MENU_TEXT_SPRITES': '9',  # trade.c enum
    'SCRIPT_BUFFER_SIZE': '128',   # quest_log.c (512 / 4)
    # These are enum values or file-local constants in .c files --
    # can't #define them globally without conflicting with definitions.
}


def resolve_file_local_constants(text: str) -> str:
    """Replace file-local constant names with their literal values."""
    for name, value in FILE_LOCAL_CONSTANTS.items():
        text = re.sub(rf'\b{re.escape(name)}\b', value, text)
    return text


def field_declaration(gv: GlobalVar) -> str:
    """Return the C struct field declaration for a global variable."""
    if gv.is_anon_struct:
        body = gv.anon_struct_body.strip()
        if gv.is_ptr:
            return f'    {body} *{gv.name};'
        elif gv.array_dims:
            dims = resolve_file_local_constants(gv.array_dims)
            return f'    {body} {gv.name}{dims};'
        else:
            return f'    {body} {gv.name};'

    if gv.is_func_ptr:
        # Re-derive func_ptr_decl with the (possibly renamed) name
        m = re.match(r'^(.+?)\s*\(\s*\*\s*\w+\s*\)\s*(\(.*\))$', gv.func_ptr_decl)
        if m:
            ret_type = m.group(1).strip()
            params = m.group(2)
            return f'    {ret_type} (*{gv.name}){params};'
        return f'    {gv.func_ptr_decl};'

    if gv.is_ptr_to_array:
        # Re-derive ptr_array_decl with the (possibly renamed) name
        m = re.match(r'^(.+?)\s*\(\s*\*\s*\w+\s*\)\s*(\[.*\])$', gv.ptr_array_decl)
        if m:
            base_type = m.group(1).strip()
            arr_dim = m.group(2)
            return f'    {base_type} (*{gv.name}){arr_dim};'
        return f'    {gv.ptr_array_decl};'

    if gv.array_dims:
        dims = resolve_file_local_constants(gv.array_dims)
        return f'    {gv.type_str} {gv.name}{dims};'

    return f'    {gv.type_str} {gv.name};'


def generate_game_ctx_h(globals_list: List[GlobalVar]) -> str:
    """Generate game_ctx.h content, grouped by source file.

    This header is included AFTER all original #include lines in transformed
    .c files, so its own includes are safe (just redundant no-ops via guards).
    It includes game_ctx_types.h for file-local type/constant definitions.
    """
    lines = [
        '#ifndef GAME_CTX_H',
        '#define GAME_CTX_H',
        '',
        '/* Auto-generated by gen_game_ctx.py -- DO NOT EDIT */',
        '',
        '/* --- Standard upstream headers for type definitions --- */',
        '#include "global.h"',
        '#include "gflib.h"',
        '#include "battle.h"',
        '#include "battle_anim.h"',
        '#include "battle_controllers.h"',
        '#include "battle_message.h"',
        '#include "sprite.h"',
        '#include "palette.h"',
        '#include "scanline_effect.h"',
        '#include "link.h"',
        '#include "save.h"',
        '#include "pokemon_storage_system.h"',
        '/* NOT including pokemon_storage_system_internal.h -- its enums conflict */',
        '#include "quest_log.h"',
        '#include "malloc.h"',
        '#include "string_util.h"',
        '#include "trade.h"',
        '#include "field_weather.h"',
        '#include "event_object_movement.h"',
        '#include "overworld.h"',
        '#include "fieldmap.h"',
        '#include "help_system.h"',
        '#include "item.h"',
        '#include "item_menu.h"',
        '/* NOT including shop.h -- pulls in menu_helpers.h conflicting decls */',
        '#include "sound.h"',
        '#include "party_menu.h"',
        '#include "window.h"',
        '#include "trainer_card.h"',
        '#include "union_room.h"',
        '#include "menu_indicators.h"',
        '#include "player_pc.h"',
        '#include "script.h"',
        '#include "libgcnmultiboot.h"',
        '#include "dodrio_berry_picking.h"',
        '#include "constants/daycare.h"',
        '',
        '/* File-local type/constant definitions needed by GameCtx members */',
        '#include "game_ctx_types.h"',
        '',
        'typedef struct GameCtx {',
    ]

    # Group by source file, preserving order of first appearance
    file_order: List[str] = []
    by_file: Dict[str, List[GlobalVar]] = defaultdict(list)
    for gv in globals_list:
        if gv.source_file not in by_file:
            file_order.append(gv.source_file)
        by_file[gv.source_file].append(gv)

    for src_file in file_order:
        gvs = by_file[src_file]
        lines.append(f'    /* --- from {src_file} --- */')
        for gv in gvs:
            decl = field_declaration(gv)
            lines.append(decl)
        lines.append('')

    lines.extend([
        '} GameCtx;',
        '',
        'extern __thread GameCtx *g_ctx;',
        '',
        'GameCtx *game_ctx_alloc(void);',
        'void game_ctx_free(GameCtx *ctx);',
        '',
        '#endif',
        '',
    ])

    return '\n'.join(lines)


def generate_game_ctx_macros_h(globals_list: List[GlobalVar]) -> str:
    """Generate game_ctx_macros.h content.

    ONLY generates macros for non-static globals.
    Static variables are NOT macro-redirected here -- they need
    per-file handling since multiple files can have static vars
    with the same name.
    """
    lines = [
        '#ifndef GAME_CTX_MACROS_H',
        '#define GAME_CTX_MACROS_H',
        '',
        '/* Auto-generated by gen_game_ctx.py -- DO NOT EDIT */',
        '',
        '/* Non-static (globally visible) names */',
    ]

    for gv in globals_list:
        if gv.is_static:
            continue
        lines.append(f'#define {gv.name} (g_ctx->{gv.name})')

    lines.extend([
        '',
        '/* Static names are NOT macro-redirected here.',
        ' * They will be handled by per-file #define in transformed source files,',
        ' * since multiple files can have `static` vars with the same name. */',
        '',
        '#endif',
        '',
    ])

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Source transformation
# ---------------------------------------------------------------------------

def parse_game_ctx_types_structs(types_h_path: Path) -> Set[str]:
    """Parse game_ctx_types.h and return the set of struct/union names defined there.

    These are the types that need #ifndef GAME_CTX_H guards in transformed .c
    files to avoid redefinition (since game_ctx.h includes game_ctx_types.h).
    """
    result: Set[str] = set()
    if not types_h_path.is_file():
        return result
    try:
        text = types_h_path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return result
    # Match lines like: struct FooBar  or  union BazQux  (start of definition)
    for m in re.finditer(r'^(?:struct|union)\s+(\w+)\s*$', text, re.MULTILINE):
        result.add(m.group(1))
    for m in re.finditer(r'^(?:struct|union)\s+(\w+)\s*\{', text, re.MULTILINE):
        result.add(m.group(1))
    return result


def find_last_include_line(lines: List[str]) -> int:
    """Find the index of the last top-level #include line in the file.
    Skips #include lines inside #ifdef/#ifndef/#if blocks.
    Returns -1 if no #include found.
    """
    last_idx = -1
    depth = 0  # preprocessor nesting depth
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('#if') and not stripped.startswith('#include'):
            depth += 1
        elif stripped.startswith('#endif'):
            depth = max(0, depth - 1)
        elif stripped.startswith('#include') and depth == 0:
            last_idx = i
    return last_idx


def is_ewram_data_line(line: str) -> bool:
    """Check if a line contains an EWRAM_DATA declaration."""
    stripped = line.strip()
    return 'EWRAM_DATA' in stripped


def find_ewram_line_ranges(lines: List[str]) -> List[Tuple[int, int]]:
    """Find all line ranges that are EWRAM_DATA definitions.
    Returns list of (start_idx, end_idx) inclusive ranges.

    Handles:
    - Single-line EWRAM_DATA declarations
    - Multi-line anonymous struct/union EWRAM_DATA declarations
    - Closing-brace form: } EWRAM_DATA ...
    """
    ranges = []
    handled = set()

    # Pass 1: Multi-line anonymous struct/union
    for i, line in enumerate(lines):
        if i in handled:
            continue
        stripped = line.strip()
        if 'EWRAM_DATA' not in stripped:
            continue

        # Check for anonymous struct/union start
        m = re.match(
            r'^(?:static\s+)?'
            r'(?:ALIGNED\(\d+\)\s+)?'
            r'(?:EWRAM_DATA)\s+'
            r'(?:static\s+)?'
            r'(?:struct|union)\s*\{?\s*$',
            stripped
        )
        if not m:
            m = re.match(
                r'^(?:static\s+)?'
                r'(?:ALIGNED\(\d+\)\s+)?'
                r'(?:EWRAM_DATA)\s+'
                r'(?:static\s+)?'
                r'(?:struct|union)\s*\{',
                stripped
            )
            if m:
                after = re.search(r'(?:EWRAM_DATA)\s+(?:static\s+)?(?:struct|union)\s*(\{|\w)', line)
                if after and after.group(1) != '{':
                    m = None

        if not m:
            continue

        brace_start = find_anon_struct_start(lines, i)
        _, end_idx = collect_anon_struct_body(lines, brace_start)

        for j in range(i, end_idx + 1):
            handled.add(j)
        ranges.append((i, end_idx))

    # Pass 1b: Closing-brace form: } [static] EWRAM_DATA ...
    for i, line in enumerate(lines):
        if i in handled:
            continue
        stripped = line.strip()
        m = RE_CLOSING_BRACE.match(stripped)
        if not m:
            continue

        # Find the opening brace by scanning backwards
        struct_start = None
        depth = 1
        for j in range(i - 1, -1, -1):
            if j in handled:
                continue
            depth += lines[j].count('}') - lines[j].count('{')
            if depth <= 0:
                struct_start = j
                # Check lines above for struct/union keyword
                for k in range(j, max(j - 3, -1), -1):
                    if 'struct' in lines[k] or 'union' in lines[k]:
                        struct_start = k
                        break
                break

        if struct_start is not None:
            for j in range(struct_start, i + 1):
                handled.add(j)
            ranges.append((struct_start, i))
        else:
            handled.add(i)
            ranges.append((i, i))

    # Pass 2: Single-line EWRAM_DATA declarations
    for i, line in enumerate(lines):
        if i in handled:
            continue
        stripped = line.strip()
        if 'EWRAM_DATA' not in stripped:
            continue
        m = RE_EWRAM_LINE.match(stripped)
        if m:
            handled.add(i)
            ranges.append((i, i))

    return sorted(ranges)


def build_replacement_map(all_globals: List[GlobalVar],
                          this_file: str) -> Dict[str, str]:
    """Build a map from original_name -> g_ctx->member_name for all globals
    that should be replaced in a given file.

    For non-static globals: replace in ALL files
    For static globals: only replace in the file that defines them
    """
    replacements = {}
    for gv in all_globals:
        if gv.is_static:
            # Only replace statics in their own source file
            if gv.source_file == this_file:
                replacements[gv.original_name] = f'g_ctx->{gv.name}'
        else:
            # Replace non-statics in all transformed files
            replacements[gv.original_name] = f'g_ctx->{gv.name}'
    return replacements


def replace_globals_in_text(text: str, replacements: Dict[str, str]) -> str:
    """Replace all word-boundary occurrences of global names with g_ctx-> access.

    Uses a fast approach: matches ALL C identifiers and checks each against
    a dict lookup. This avoids building a huge alternation regex.

    Carefully avoids replacing inside:
    - String literals (double-quoted)
    - Single-character literals (single-quoted)
    - C-style block comments
    - C++ style line comments
    - Lines starting with 'extern' (extern declarations)
    """
    if not replacements:
        return text

    # Tokenizer regex: matches strings, comments, identifiers, or any other char
    # Order matters: strings/comments before identifiers
    TOKEN_RE = re.compile(
        r'//[^\n]*'           # C++ line comment
        r'|/\*[\s\S]*?\*/'    # C block comment (non-greedy)
        r'|"(?:[^"\\]|\\.)*"' # double-quoted string
        r"|'(?:[^'\\]|\\.)*'" # single-quoted char
        r'|\b[a-zA-Z_]\w*\b'  # C identifier
        r'|[\s\S]'            # any other single character
    )

    def _replace_token(m: re.Match) -> str:
        token = m.group(0)
        if token in replacements:
            return replacements[token]
        return token

    # Process line-by-line for context awareness
    output_lines = []
    for line in text.split('\n'):
        stripped = line.strip()

        # Skip extern declarations entirely (don't replace in them)
        if stripped.startswith('extern ') or stripped.startswith('extern\t'):
            output_lines.append(line)
            continue

        output_lines.append(TOKEN_RE.sub(_replace_token, line))

    return '\n'.join(output_lines)


def fix_static_initializers(text: str) -> str:
    """Fix static array/struct initializers that contain g_ctx-> references.

    C requires file-scope variable initializers to be constant expressions.
    When EWRAM globals are rewritten to g_ctx->member, &g_ctx->member and
    g_ctx->member are no longer constant expressions (g_ctx is a __thread
    pointer).

    This function finds such initializers and converts them to a getter-macro
    pattern:

      static struct T sArr[] = { {&g_ctx->x, 1}, {&g_ctx->y, 2} };
        becomes:
      static struct T _sArr_storage[2];
      static inline struct T *_sArr_init(void) {
          _sArr_storage[0] = (struct T){&g_ctx->x, 1};
          _sArr_storage[1] = (struct T){&g_ctx->y, 2};
          return _sArr_storage;
      }
      #define sArr _sArr_init()

    This is transparent to all call sites.
    """
    lines = text.split('\n')
    output_lines = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Detect file-scope array/struct initializers that may contain g_ctx->:
        #   static [const] TYPE NAME[...] = {
        #   [const] TYPE NAME[...] = {         (non-static file-scope, at col 0)
        # Also handle: = on one line, { on the next line
        #
        # We use a regex that makes 'static' optional. For non-static matches,
        # we verify the line is at column 0 (file scope, not function-local).
        #
        # Pattern 1: = { on same line
        m = re.match(
            r'^(\s*)'                        # (1) leading whitespace
            r'(static\s+)?'                  # (2) optional static
            r'(const\s+)?'                   # (3) optional const
            r'((?:struct\s+|union\s+)?'      # (4) type (possibly struct/union)
            r'\w[\w\s*]*?)'                  # type name
            r'\s+'                           #
            r'(\w+)'                         # (5) variable name
            r'\s*'                           #
            r'(\[\s*\w*\s*\](?:\[\s*\w*\s*\])*)?'  # (6) optional array dimensions
            r'\s*=\s*\{',                    # = {
            stripped
        )
        # Pattern 2: = at end of line, { on next line
        m2 = None
        if not m:
            m2 = re.match(
                r'^(\s*)'                        # (1) leading whitespace
                r'(static\s+)?'                  # (2) optional static
                r'(const\s+)?'                   # (3) optional const
                r'((?:struct\s+|union\s+)?'      # (4) type (possibly struct/union)
                r'\w[\w\s*]*?)'                  # type name
                r'\s+'                           #
                r'(\w+)'                         # (5) variable name
                r'\s*'                           #
                r'(\[\s*\w*\s*\](?:\[\s*\w*\s*\])*)?'  # (6) optional array dimensions
                r'\s*=\s*$',                     # = (end of line)
                stripped
            )
            if m2 and i + 1 < len(lines) and lines[i + 1].strip().startswith('{'):
                m = m2  # Use pattern 2 match (same groups)
            else:
                m2 = None

        # For non-static matches, only apply at file scope (column 0)
        if m and not m.group(2):
            # No 'static' keyword -- check if at column 0 (file scope)
            if line[0:1] in (' ', '\t'):
                # Indented => likely function-local, skip
                m = None

        if not m:
            output_lines.append(line)
            i += 1
            continue

        indent = m.group(1)
        static_qual = m.group(2) or ''
        const_qual = m.group(3) or ''
        type_str = m.group(4).strip()
        var_name = m.group(5)
        dims_str = m.group(6) or ''

        # Collect the full initializer block (find matching closing brace + semicolon)
        init_lines = [line]
        brace_depth = 0
        found_end = False
        j = i
        while j < len(lines):
            for ch in lines[j]:
                if ch == '{':
                    brace_depth += 1
                elif ch == '}':
                    brace_depth -= 1
            if brace_depth <= 0 and lines[j].rstrip().endswith(';'):
                found_end = True
                if j > i:
                    init_lines = lines[i:j+1]
                break
            j += 1

        if not found_end:
            output_lines.append(line)
            i += 1
            continue

        block_text = '\n'.join(init_lines)

        # Only apply if the block contains g_ctx->
        if 'g_ctx->' not in block_text:
            for bl in init_lines:
                output_lines.append(bl)
            i = j + 1
            continue

        # Parse the initializer elements (top-level comma-separated or brace-groups)
        # Extract everything between the outermost { ... };
        init_match = re.search(r'=\s*\{([\s\S]*)\}\s*;', block_text)
        if not init_match:
            for bl in init_lines:
                output_lines.append(bl)
            i = j + 1
            continue

        init_body = init_match.group(1).strip()

        # Split into top-level elements (respecting nested braces)
        elements = []
        current = []
        depth = 0
        for ch in init_body:
            if ch == '{':
                depth += 1
                current.append(ch)
            elif ch == '}':
                depth -= 1
                current.append(ch)
            elif ch == ',' and depth == 0:
                elements.append(''.join(current).strip())
                current = []
            else:
                current.append(ch)
        if current:
            last = ''.join(current).strip()
            if last:
                elements.append(last)

        count = len(elements)
        if count == 0:
            for bl in init_lines:
                output_lines.append(bl)
            i = j + 1
            continue

        # Determine if elements are brace-enclosed (struct initializers) or
        # designated initializers
        first_elem = elements[0].strip()
        is_struct_init = first_elem.startswith('{')
        has_designated = first_elem.startswith('[')

        # Build the getter-macro replacement
        storage_name = f'__{var_name}_storage'
        init_fn_name = f'__{var_name}_init'

        # Determine array size string
        # dims_str like "[]" means unspecified size -- use computed count
        # dims_str like "[10]" means explicit size -- keep it
        if dims_str and not re.match(r'^\[\s*\]$', dims_str):
            size_str = dims_str
        else:
            size_str = f'[{count}]'

        # Remove const for storage (needs to be writable for runtime init)
        # Also strip 'const' from within the type itself (e.g. "u8 *const" -> "u8 *")
        storage_type = re.sub(r'\bconst\b\s*', '', type_str).strip()
        output_lines.append(f'{indent}static {storage_type} {storage_name}{size_str};')
        output_lines.append(f'{indent}static inline {storage_type} *{init_fn_name}(void) {{')

        for idx, elem in enumerate(elements):
            elem = elem.strip()
            if elem.startswith('['):
                # Designated initializer like [ENUM] = { ... } or [ENUM] = val
                des_m = re.match(r'\[([^\]]+)\]\s*=\s*\{(.*)\}', elem, re.DOTALL)
                if des_m:
                    des_idx = des_m.group(1).strip()
                    inner = des_m.group(2).strip()
                    output_lines.append(
                        f'{indent}    {storage_name}[{des_idx}] = ({storage_type}){{{inner}}};')
                else:
                    # Designated initializer without braces
                    des_m2 = re.match(r'\[([^\]]+)\]\s*=\s*(.*)', elem, re.DOTALL)
                    if des_m2:
                        des_idx = des_m2.group(1).strip()
                        val = des_m2.group(2).strip()
                        output_lines.append(
                            f'{indent}    {storage_name}[{des_idx}] = {val};')
                    else:
                        output_lines.append(
                            f'{indent}    {storage_name}[{idx}] = {elem};')
            elif (is_struct_init or has_designated) and \
                    elem.startswith('{') and elem.endswith('}'):
                # Struct initializer element
                inner = elem[1:-1].strip()
                output_lines.append(
                    f'{indent}    {storage_name}[{idx}] = ({storage_type}){{{inner}}};')
            else:
                # Simple element
                output_lines.append(
                    f'{indent}    {storage_name}[{idx}] = {elem};')

        output_lines.append(f'{indent}    return {storage_name};')
        output_lines.append(f'{indent}}}')
        output_lines.append(f'{indent}#define {var_name} {init_fn_name}()')
        # NELEMS/ARRAY_COUNT on a function-call macro gives wrong sizeof.
        # Emit a size constant so we can replace NELEMS(name) later.
        output_lines.append(
            f'{indent}#define _GAMECTX_NELEMS_{var_name} {count}')

        i = j + 1
        continue

    result = '\n'.join(output_lines)

    # Replace NELEMS(name) and ARRAY_COUNT(name) for any getter-macro'd arrays
    # with the pre-computed size constant.
    result = re.sub(
        r'(?:NELEMS|ARRAY_COUNT)\(\s*(\w+)\s*\)',
        lambda m: f'_GAMECTX_NELEMS_{m.group(1)}'
                  if f'_GAMECTX_NELEMS_{m.group(1)}' in result
                  else m.group(0),
        result
    )

    return result


def transform_source(src_path: Path, gen_dir: Path,
                     all_globals: List[GlobalVar],
                     files_with_ewram: Set[str],
                     fallback_src_dir: Optional[Path] = None,
                     force: bool = False,
                     game_ctx_types_structs: Optional[Set[str]] = None,
                     excluded_names: Optional[Set[str]] = None) -> Optional[Path]:
    """Transform a .c source file for GameCtx:
    1. Read the original (from gen_dir if it exists there, else from src_path)
    2. Remove EWRAM_DATA definitions
    3. After the last #include, insert game_ctx.h inclusion
    4. Replace global variable references with g_ctx-> access
    5. Write to gen_dir/basename.c

    If the file already exists in gen_dir (e.g. from charmap preprocessing),
    read from there instead to preserve charmap/INCBIN processing.

    If force=True, re-transform even if already transformed (reads from
    original source instead of the already-transformed gen/ file).

    Returns the output path. For files without EWRAM_DATA, injects game_ctx includes only.
    """
    source_file = src_path.name
    has_ewram = source_file in files_with_ewram

    # Check if a preprocessed version already exists in gen_dir
    gen_path = gen_dir / source_file
    if gen_path.is_file() and gen_path != src_path:
        read_path = gen_path
    else:
        read_path = src_path

    try:
        text = read_path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        print(f'WARNING: cannot read {read_path}', file=sys.stderr)
        return None

    # Skip if already transformed (unless --force)
    if 'GameCtx-transformed' in text[:200]:
        if force:
            # Re-read from original source to re-transform from scratch
            read_path = src_path
            try:
                text = read_path.read_text(encoding='utf-8', errors='replace')
            except OSError:
                print(f'WARNING: cannot read original {read_path}', file=sys.stderr)
                return None
            print(f'  [force] Re-transforming {source_file} from {read_path}',
                  file=sys.stderr)
        else:
            return gen_path

    lines = text.splitlines()

    # For files without EWRAM_DATA, just inject the game_ctx includes
    if not has_ewram:
        last_include = -1
        for i, line in enumerate(lines):
            if re.match(r'^\s*#\s*include\s', line):
                last_include = i
        if last_include >= 0:
            inject = [
                "",
                "/* --- GameCtx: struct definition and g_ctx pointer --- */",
                '#include "game_ctx.h"',
                '#include "game_ctx_macros.h"',
            ]
            lines = lines[:last_include + 1] + inject + lines[last_include + 1:]
        header_line = f"/* GameCtx-injected from {source_file} -- AUTO-GENERATED */"
        lines.insert(0, header_line)
        gen_path = gen_dir / source_file
        gen_path.write_text(chr(10).join(lines) + chr(10), encoding='utf-8')
        return gen_path

    # Build replacement map for this file
    replacements = build_replacement_map(all_globals, source_file)

    # Find EWRAM_DATA line ranges to remove
    ewram_ranges = find_ewram_line_ranges(lines)

    # Filter out ranges for excluded globals (keep them as regular EWRAM_DATA)
    excl = excluded_names or set()
    if excl:
        filtered_ranges = []
        for start, end in ewram_ranges:
            # Check if any excluded name appears in the EWRAM_DATA line(s)
            range_text = ' '.join(lines[start:end + 1])
            if any(re.search(rf'\b{re.escape(name)}\b', range_text)
                   for name in excl):
                continue  # Keep this EWRAM_DATA line (don't remove)
            filtered_ranges.append((start, end))
        ewram_ranges = filtered_ranges

    ewram_line_set = set()
    for start, end in ewram_ranges:
        for i in range(start, end + 1):
            ewram_line_set.add(i)

    # Find file-local struct definitions that game_ctx_types.h also defines.
    # These must be REMOVED from the transformed .c file to avoid redefinition,
    # since game_ctx_types.h (included via game_ctx.h) provides them.
    # ONLY remove structs that are actually in game_ctx_types.h -- not all
    # structs referenced by EWRAM_DATA variables (some are only used via pointer
    # and don't need to be in game_ctx_types.h).
    local_struct_types = game_ctx_types_structs if game_ctx_types_structs else set()

    # Find line ranges for file-local struct definitions to remove
    struct_remove_ranges: List[Tuple[int, int]] = []  # (start, end) inclusive
    for i, line in enumerate(lines):
        if i in ewram_line_set:
            continue
        stripped = line.strip()
        for stype in local_struct_types:
            if re.match(rf'^(?:struct|union)\s+{re.escape(stype)}\s*$', stripped) or \
               re.match(rf'^(?:struct|union)\s+{re.escape(stype)}\s*\{{', stripped):
                # Found a struct definition. Find its end.
                brace_start = find_anon_struct_start(lines, i)
                _, end_idx = collect_anon_struct_body(lines, brace_start)
                # Check the line after the closing brace
                if end_idx < len(lines) - 1:
                    next_line = lines[end_idx + 1].strip() if end_idx + 1 < len(lines) else ''
                    # If next line has EWRAM_DATA, this is an anon struct - skip
                    if 'EWRAM_DATA' in next_line:
                        continue
                # Only remove if the closing line is just `};`
                if lines[end_idx].strip().startswith('}'):
                    struct_remove_ranges.append((i, end_idx))

    struct_remove_set = set()
    for start, end in struct_remove_ranges:
        for j in range(start, end + 1):
            struct_remove_set.add(j)

    # Find last #include line
    last_include = find_last_include_line(lines)

    # Build output lines
    out_lines = []
    # Header comment
    out_lines.append(f'/* GameCtx-transformed from {source_file} -- AUTO-GENERATED */')

    for i, line in enumerate(lines):
        if i in ewram_line_set:
            # Replace EWRAM_DATA definition with a comment
            # Only add comment for the first line of each range
            is_range_start = any(start == i for start, end in ewram_ranges)
            if is_range_start:
                out_lines.append(f'/* GAMECTX: moved to GameCtx */')
            # Skip all other lines in the range
            continue

        # Remove file-local struct definitions that are now in game_ctx_types.h
        if i in struct_remove_set:
            # Only add comment for the first line of each range
            is_range_start = any(start == i for start, end in struct_remove_ranges)
            if is_range_start:
                out_lines.append(f'/* GAMECTX: struct now in game_ctx_types.h */')
            continue

        out_lines.append(line)

        # After the last #include, insert game_ctx.h
        if i == last_include:
            out_lines.append('')
            out_lines.append('/* --- GameCtx: struct definition and g_ctx pointer --- */')
            out_lines.append('#include "game_ctx.h"')

    # Join and apply global replacements
    output_text = '\n'.join(out_lines) + '\n'
    output_text = replace_globals_in_text(output_text, replacements)

    # Fix static initializers that now contain g_ctx-> (not constant expressions)
    output_text = fix_static_initializers(output_text)

    # Write output
    out_path = gen_dir / source_file
    out_path.write_text(output_text, encoding='utf-8')
    return out_path


def generate_game_ctx_stubs_c(globals_list: List[GlobalVar]) -> str:
    """Generate game_ctx_stubs.c with stub definitions for all non-static globals.

    These stubs provide symbol definitions that satisfy extern declarations
    in upstream headers. Non-transformed .c files that reference these globals
    will link against these stubs.

    The stubs are zero-initialized (like the EWRAM_DATA originals).
    """
    lines = [
        '/* Auto-generated by gen_game_ctx.py -- DO NOT EDIT */',
        '/*',
        ' * Stub definitions for EWRAM_DATA globals that have been moved to GameCtx.',
        ' * These satisfy extern declarations in upstream headers for files that',
        ' * have not yet been source-transformed.',
        ' *',
        ' * As more files are transformed, these stubs become less necessary.',
        ' * Eventually, when ALL files are transformed, this file can be removed.',
        ' */',
        '',
        '#include "global.h"',
        '#include "gflib.h"',
        '#include "battle.h"',
        '#include "sprite.h"',
        '#include "palette.h"',
        '#include "scanline_effect.h"',
        '#include "link.h"',
        '#include "save.h"',
        '#include "pokemon_storage_system.h"',
        '#include "quest_log.h"',
        '#include "malloc.h"',
        '#include "string_util.h"',
        '#include "trade.h"',
        '#include "field_weather.h"',
        '#include "event_object_movement.h"',
        '#include "overworld.h"',
        '#include "fieldmap.h"',
        '#include "help_system.h"',
        '#include "item.h"',
        '#include "shop.h"',
        '#include "sound.h"',
        '#include "party_menu.h"',
        '#include "window.h"',
        '#include "trainer_card.h"',
        '#include "union_room.h"',
        '#include "battle_controllers.h"',
        '#include "battle_anim.h"',
        '#include "battle_message.h"',
        '#include "item_menu.h"',
        '#include "player_pc.h"',
        '#include "game_ctx_types.h"',
        '',
    ]

    for gv in globals_list:
        if gv.is_static:
            continue
        if gv.is_anon_struct:
            # Anonymous structs can't be easily stubbed; skip
            # (they are typically static anyway)
            lines.append(f'/* SKIP anon struct: {gv.name} */')
            continue
        if gv.is_func_ptr:
            lines.append(f'{gv.func_ptr_decl} = NULL;')
        elif gv.is_ptr_to_array:
            lines.append(f'{gv.ptr_array_decl} = NULL;')
        elif gv.array_dims:
            lines.append(f'{gv.type_str} {gv.name}{gv.array_dims} = {{0}};')
        else:
            # Check if it's a pointer type
            if '*' in gv.type_str:
                lines.append(f'{gv.type_str} {gv.name} = NULL;')
            elif gv.type_str.startswith('struct ') or gv.type_str.startswith('union '):
                lines.append(f'{gv.type_str} {gv.name} = {{0}};')
            else:
                # Scalar type: use = 0
                lines.append(f'{gv.type_str} {gv.name} = 0;')

    lines.append('')
    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# Stats
# ---------------------------------------------------------------------------

def global_kind(gv: GlobalVar) -> str:
    """Return a compact kind label for inventory reporting."""
    if gv.is_anon_struct:
        return 'anon_struct'
    if gv.is_func_ptr:
        return 'func_ptr'
    if gv.is_ptr_to_array:
        return 'ptr_to_array'
    return 'plain'


def inventory_type_string(gv: GlobalVar) -> str:
    """Return a readable type string for inventory reporting."""
    if gv.is_anon_struct:
        body = re.sub(r'\s+', ' ', gv.anon_struct_body.strip())
        return f'{body} *' if gv.is_ptr else body
    if gv.is_func_ptr:
        return gv.func_ptr_decl
    if gv.is_ptr_to_array:
        return gv.ptr_array_decl
    suffix = resolve_file_local_constants(gv.array_dims) if gv.array_dims else ''
    return f'{gv.type_str}{suffix}'


def generate_inventory_report(globals_list: List[GlobalVar]) -> str:
    """Generate a human-readable inventory report for GameCtx fields."""
    lines = [
        '# Auto-generated by gen_game_ctx.py -- DO NOT EDIT',
        '# source_file\tline_no\tscope\tstorage\toriginal_name\tmember_name\tkind\ttype\traw_decl',
    ]

    for gv in globals_list:
        scope = 'file' if gv.is_file_scope else 'function'
        storage = 'static' if gv.is_static else 'global'
        original_name = gv.original_name or gv.name
        raw_decl = gv.raw_line.replace('\n', '\\n')
        lines.append('\t'.join([
            gv.source_file,
            str(gv.line_no),
            scope,
            storage,
            original_name,
            gv.name,
            global_kind(gv),
            inventory_type_string(gv),
            raw_decl,
        ]))

    lines.append('')
    return '\n'.join(lines)


def build_summary(raw_globals: List[GlobalVar],
                  final_globals: List[GlobalVar],
                  section_usage: Dict[str, List[SectionMacroUse]],
                  static_collisions: Dict[str, List[str]],
                  duplicate_nonstatic: Dict[str, List[str]]) -> Dict[str, object]:
    """Build a JSON-serializable summary of GameCtx inventory findings."""
    section_summary = {}
    for macro in SECTION_MACROS:
        uses = section_usage.get(macro, [])
        section_summary[macro] = {
            'uses': len(uses),
            'file_scope_uses': sum(1 for use in uses if use.is_file_scope),
            'function_scope_uses': sum(1 for use in uses if not use.is_file_scope),
            'files': len({use.source_file for use in uses}),
        }

    return {
        'raw_ewram_uses': len(raw_globals),
        'raw_ewram_file_scope_uses': sum(1 for gv in raw_globals if gv.is_file_scope),
        'raw_ewram_function_scope_uses': sum(1 for gv in raw_globals if not gv.is_file_scope),
        'final_game_ctx_fields': len(final_globals),
        'final_file_scope_fields': sum(1 for gv in final_globals if gv.is_file_scope),
        'final_function_scope_fields': sum(1 for gv in final_globals if not gv.is_file_scope),
        'non_static_fields': sum(1 for gv in final_globals if not gv.is_static),
        'static_fields': sum(1 for gv in final_globals if gv.is_static),
        'anonymous_struct_fields': sum(1 for gv in final_globals if gv.is_anon_struct),
        'function_pointer_fields': sum(1 for gv in final_globals if gv.is_func_ptr),
        'pointer_to_array_fields': sum(1 for gv in final_globals if gv.is_ptr_to_array),
        'renamed_fields': sum(1 for gv in final_globals if gv.original_name and gv.name != gv.original_name),
        'source_files_with_ewram': len({gv.source_file for gv in raw_globals}),
        'section_macros': section_summary,
        'static_name_collisions': static_collisions,
        'duplicate_nonstatic_globals': duplicate_nonstatic,
        'excluded_globals': [
            {'source_file': source_file, 'name': name}
            for source_file, name in sorted(EXCLUDED_GLOBALS)
        ],
    }


def print_stats(globals_list: List[GlobalVar]) -> None:
    """Print summary statistics."""
    total = len(globals_list)
    static_count = sum(1 for g in globals_list if g.is_static)
    nonstatic_count = total - static_count
    anon_count = sum(1 for g in globals_list if g.is_anon_struct)
    func_ptr_count = sum(1 for g in globals_list if g.is_func_ptr)
    ptr_arr_count = sum(1 for g in globals_list if g.is_ptr_to_array)
    renamed = sum(1 for g in globals_list if g.original_name and g.name != g.original_name)
    files = set(g.source_file for g in globals_list)

    print(f'=== gen_game_ctx.py stats ===')
    print(f'Source files with EWRAM_DATA: {len(files)}')
    print(f'Total globals found:          {total}')
    print(f'  file-scope:                 {sum(1 for g in globals_list if g.is_file_scope)}')
    print(f'  function-scope:             {sum(1 for g in globals_list if not g.is_file_scope)}')
    print(f'  non-static:                 {nonstatic_count}')
    print(f'  static:                     {static_count}')
    print(f'  anonymous struct/union:      {anon_count}')
    print(f'  function pointers:           {func_ptr_count}')
    print(f'  pointer-to-array:            {ptr_arr_count}')
    print(f'  renamed (dedup collision):   {renamed}')


def print_section_macro_stats(section_usage: Dict[str, List[SectionMacroUse]]) -> None:
    """Print a compact summary of storage-section macro usage."""
    print('=== section macro usage ===')
    for macro in SECTION_MACROS:
        uses = section_usage.get(macro, [])
        file_scope = sum(1 for use in uses if use.is_file_scope)
        function_scope = len(uses) - file_scope
        print(f'{macro:>11}: {len(uses):4d} uses across {len({use.source_file for use in uses}):3d} files'
              f' ({file_scope} file-scope, {function_scope} function-scope)')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description='Generate GameCtx struct and macros from EWRAM_DATA globals')
    parser.add_argument('--src-dir', type=Path, required=True,
                        help='Path to third_party/pokefirered/src')
    parser.add_argument('--host-src-dir', type=Path, required=True,
                        help='Path to src/ (for upstream_stubs.c)')
    parser.add_argument('--out-dir', type=Path, required=True,
                        help='Directory to write generated headers')
    parser.add_argument('--gen-dir', type=Path, default=None,
                        help='Directory to write transformed .c files (default: out-dir/../gen)')
    parser.add_argument('--inventory-out', type=Path, default=None,
                        help='Optional path to write a detailed inventory report')
    parser.add_argument('--summary-out', type=Path, default=None,
                        help='Optional path to write a JSON summary report')
    parser.add_argument('--transform', action='store_true',
                        help='Generate transformed .c source files in gen-dir')
    parser.add_argument('--force', action='store_true',
                        help='Force re-transformation of already-transformed files')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print stats without writing files')
    args = parser.parse_args()

    src_dir = args.src_dir.resolve()
    host_src_dir = args.host_src_dir.resolve()
    out_dir = args.out_dir.resolve()
    gen_dir = (args.gen_dir or out_dir.parent / 'gen').resolve()

    if not src_dir.is_dir():
        print(f'ERROR: --src-dir does not exist: {src_dir}', file=sys.stderr)
        sys.exit(1)

    if not host_src_dir.is_dir():
        print(f'ERROR: --host-src-dir does not exist: {host_src_dir}',
              file=sys.stderr)
        sys.exit(1)

    # Scan
    all_globals = scan_all(src_dir, host_src_dir)
    section_usage = scan_section_macro_usage(src_dir, host_src_dir)
    static_collisions = find_static_collisions(all_globals)
    duplicate_nonstatic = find_duplicate_nonstatic(all_globals)
    print(f'Raw globals found: {len(all_globals)}')

    # Deduplicate
    deduped = deduplicate(all_globals)
    print(f'After deduplication: {len(deduped)}')

    # Remove excluded globals
    if EXCLUDED_GLOBALS:
        before = len(deduped)
        deduped = [gv for gv in deduped
                   if (gv.source_file, gv.original_name) not in EXCLUDED_GLOBALS]
        excluded = before - len(deduped)
        if excluded:
            print(f'Excluded {excluded} globals (local variable shadowing)')
    print()

    # Stats
    print_stats(deduped)
    print()
    print_section_macro_stats(section_usage)
    print()

    if args.dry_run:
        print('--dry-run: not writing files.')
        print(f'Would generate {len(deduped)} fields in GameCtx.')
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    ctx_h = generate_game_ctx_h(deduped)
    macros_h = generate_game_ctx_macros_h(deduped)
    inventory_report = generate_inventory_report(deduped)
    summary = build_summary(all_globals, deduped, section_usage,
                            static_collisions, duplicate_nonstatic)

    ctx_h_path = out_dir / 'game_ctx.h'
    macros_h_path = out_dir / 'game_ctx_macros.h'

    ctx_h_path.write_text(ctx_h, encoding='utf-8')
    macros_h_path.write_text(macros_h, encoding='utf-8')

    print(f'Wrote {ctx_h_path}  ({len(ctx_h.splitlines())} lines)')
    print(f'Wrote {macros_h_path}  ({len(macros_h.splitlines())} lines)')

    if args.inventory_out is not None:
        inventory_out = args.inventory_out.resolve()
        inventory_out.parent.mkdir(parents=True, exist_ok=True)
        inventory_out.write_text(inventory_report, encoding='utf-8')
        print(f'Wrote {inventory_out}  ({len(inventory_report.splitlines())} lines)')

    if args.summary_out is not None:
        summary_out = args.summary_out.resolve()
        summary_out.parent.mkdir(parents=True, exist_ok=True)
        summary_text = json.dumps(summary, indent=2, sort_keys=True) + '\n'
        summary_out.write_text(summary_text, encoding='utf-8')
        print(f'Wrote {summary_out}  ({len(summary_text.splitlines())} lines)')

    # --- Source transformation ---
    if args.transform:
        gen_dir.mkdir(parents=True, exist_ok=True)

        # Set of filenames that have EWRAM_DATA
        files_with_ewram = set(gv.source_file for gv in deduped)

        # Parse game_ctx_types.h to know which structs need removal
        types_h_path = host_src_dir / 'game_ctx_types.h'
        gct_structs = parse_game_ctx_types_structs(types_h_path)
        if gct_structs:
            print(f'Structs in game_ctx_types.h: {sorted(gct_structs)}')

        # Build per-file excluded name sets
        excl_by_file: Dict[str, Set[str]] = defaultdict(set)
        for src_file, name in EXCLUDED_GLOBALS:
            excl_by_file[src_file].add(name)

        # Transform upstream sources
        transformed = []
        for cfile in sorted(src_dir.glob('*.c')):
            out_path = transform_source(cfile, gen_dir, deduped,
                                        files_with_ewram, force=args.force,
                                        game_ctx_types_structs=gct_structs,
                                        excluded_names=excl_by_file.get(cfile.name))
            if out_path:
                transformed.append(out_path)

        # Transform upstream_stubs.c
        stubs_file = host_src_dir / 'upstream_stubs.c'
        if stubs_file.is_file() and stubs_file.name in files_with_ewram:
            out_path = transform_source(stubs_file, gen_dir, deduped,
                                        files_with_ewram, force=args.force,
                                        game_ctx_types_structs=gct_structs,
                                        excluded_names=excl_by_file.get(stubs_file.name))
            if out_path:
                transformed.append(out_path)

        print(f'\nTransformed {len(transformed)} source files to {gen_dir}')

        # Generate game_ctx_stubs.c
        stubs_c = generate_game_ctx_stubs_c(deduped)
        stubs_c_path = gen_dir / 'game_ctx_stubs.c'
        stubs_c_path.write_text(stubs_c, encoding='utf-8')
        print(f'Wrote {stubs_c_path}  ({len(stubs_c.splitlines())} lines)')

        # Print list of transformed files for CMake integration
        print(f'\nTransformed files list:')
        for p in transformed:
            print(f'  {p.name}')


if __name__ == '__main__':
    main()
