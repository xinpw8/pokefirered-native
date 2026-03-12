#!/usr/bin/env python3
"""
script_assembler.py — GBA GAS-style script data assembler for native builds.

Converts .s assembly files (GBA battle/event scripts) to C source that can
be compiled on native 64-bit platforms.

Key design:
  - Preprocesses each .s file with gcc -E to expand C #define constants
  - Parses GAS .macro/.endm blocks from .include'd macro files
  - Assembles .byte/.2byte/.4byte directives into a single contiguous bytearray
  - Generates ONE u8 array per input file, plus GCC inline-asm symbol aliases
    so that each label becomes a proper C symbol (satisfying extern const u8 X[])
  - Generates a HostPatch*() function that writes 4-byte LE pointer fields at
    runtime (required because C can't express 64-bit → 32-bit pointer casts at
    compile time)

Usage:
    python3 tools/script_assembler.py \\
        --pokedir third_party/pokefirered \\
        --prefix pfr_bs \\
        --patch-fn HostPatchBattleScriptPointers \\
        --output src/upstream_battle_scripts.c \\
        data/battle_scripts_1.s data/battle_scripts_2.s
"""

import argparse
import os
import re
import subprocess
import sys

# ---------------------------------------------------------------------------
# Utility: expression evaluation
# ---------------------------------------------------------------------------

def safe_eval(expr: str, syms: dict) -> "int | None":
    """Evaluate a numeric expression. Returns int or None if unresolvable."""
    expr = expr.strip()
    if not expr:
        return None
    # Replace known symbols with their values
    replaced = expr
    tokens = sorted(
        {
            token for token in re.findall(r'(?:\.[A-Za-z_][\w\.]*|[A-Za-z_]\w*)', expr)
            if token in syms
        },
        key=len,
        reverse=True,
    )
    for name in tokens:
        val = syms[name]
        replaced = re.sub(
            r'(?<![A-Za-z0-9_.])' + re.escape(name) + r'(?![A-Za-z0-9_.])',
            str(val),
            replaced,
        )
    # Let eval() catch NameError for any remaining unresolvable identifiers.
    # Do NOT pre-check with a regex — that would reject valid hex literals like
    # '0x3a' because the letter 'x' matches [A-Za-z_].
    try:
        result = eval(replaced, {"__builtins__": {}})  # noqa: S307
        if isinstance(result, (int, float)):
            return int(result) & 0xFFFFFFFF
    except Exception:
        pass
    return None


def strip_outer_parens(expr: str) -> str:
    """Remove matching outer parentheses from an expression."""
    expr = expr.strip()
    while len(expr) >= 2 and expr[0] == '(' and expr[-1] == ')':
        inner = expr[1:-1]
        depth = 0
        ok = True
        for c in inner:
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth < 0:
                    ok = False
                    break
        if ok and depth == 0:
            expr = inner.strip()
        else:
            break
    return expr


def parse_4byte(expr: str, syms: dict) -> "tuple":
    """
    Parse a .4byte argument.
    Returns:
      ('num', int)          — numeric constant
      ('reloc', sym, addend) — symbol + addend relocation
    """
    expr = strip_outer_parens(expr.strip())

    # Try numeric first (may reference .set symbols)
    val = safe_eval(expr, syms)
    if val is not None:
        return ('num', val)

    # Extract leading identifier
    m = re.match(r'^((?:[A-Za-z_]\w*|\.[A-Za-z_][\w\.]*))(.*)', expr)
    if not m:
        return ('num', 0)

    sym_name = m.group(1)
    rest = m.group(2).strip()

    # If the identifier itself is in syms, substitute and retry
    if sym_name in syms:
        new_expr = str(syms[sym_name]) + (' ' + rest if rest else '')
        val = safe_eval(new_expr, syms)
        if val is not None:
            return ('num', val)

    # It's a relocation; parse the addend from `rest`
    addend = 0
    if rest:
        addend_expr = rest
        # rest is like " + 0x0E" or " + 5 + 3"
        if addend_expr and addend_expr[0] in ('+', '-'):
            addend_expr = '0 ' + addend_expr
        addend_val = safe_eval(addend_expr, syms)
        if addend_val is not None:
            addend = addend_val
        # If still not resolved, treat as 0 addend (best effort)
    return ('reloc', sym_name, addend)


# ---------------------------------------------------------------------------
# Macro parser
# ---------------------------------------------------------------------------

class Macro:
    def __init__(self, name: str, params: list, body: list):
        self.name = name   # str
        self.params = params  # list of (name, required, default, is_vararg)
        self.body = body   # list of raw line strings
        self._expand_serial = 0

    def expand(self, args: list) -> list:
        bindings = {}
        arg_index = 0
        for pname, required, default, is_vararg in self.params:
            if is_vararg:
                if arg_index < len(args):
                    bindings[pname] = ', '.join(arg.strip() for arg in args[arg_index:])
                elif default is not None:
                    bindings[pname] = default
                elif required:
                    sys.stderr.write(f"Warning: missing required vararg {pname} "
                                     f"for macro {self.name}, using empty string\n")
                    bindings[pname] = ''
                else:
                    bindings[pname] = ''
                arg_index = len(args)
                continue

            if arg_index < len(args):
                bindings[pname] = args[arg_index].strip()
                arg_index += 1
            elif default is not None:
                bindings[pname] = default
            elif required:
                sys.stderr.write(f"Warning: missing required arg {pname} "
                                 f"for macro {self.name}, using empty string\n")
                bindings[pname] = ''
            else:
                bindings[pname] = ''

        expansion_id = self._expand_serial
        self._expand_serial += 1
        expanded = []
        for line in self.body:
            result = line
            for name, val in sorted(bindings.items(), key=lambda item: -len(item[0])):
                result = result.replace(f'\\{name}', val)
            result = result.replace(r'\@', str(expansion_id))
            expanded.append(result)
        return expanded


def parse_macro_params(params_str: str) -> list:
    """Parse GAS macro params like 'name:req, argv:vararg, x=1' into tuples."""
    params = []
    if not params_str.strip():
        return params
    for p in params_str.split(','):
        p = p.strip()
        if not p:
            continue
        default = None
        if '=' in p:
            p, default = p.split('=', 1)
            default = default.strip()

        parts = [part.strip() for part in p.split(':') if part.strip()]
        pname = parts[0]
        qualifiers = set(parts[1:])
        params.append((pname, 'req' in qualifiers, default, 'vararg' in qualifiers))
    return params


def load_macros_from_lines(lines: list) -> dict:
    """Parse all .macro/.endm blocks. Returns {name: Macro}."""
    macros = {}
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        m = re.match(r'^\.macro\s+(\w+)\s*(.*)', line)
        if m:
            macro_name = m.group(1)
            params = parse_macro_params(m.group(2))
            body = []
            i += 1
            depth = 1
            while i < len(lines) and depth > 0:
                bline = lines[i].rstrip()
                stripped = bline.strip()
                if re.match(r'^\.macro\b', stripped):
                    depth += 1
                    body.append(bline)
                elif stripped == '.endm':
                    depth -= 1
                    if depth > 0:
                        body.append(bline)
                else:
                    body.append(bline)
                i += 1
            macros[macro_name] = Macro(macro_name, params, body)
        else:
            i += 1
    return macros


def load_macro_file(path: str) -> dict:
    """Load macros from a .inc file."""
    try:
        with open(path) as f:
            return load_macros_from_lines(f.read().splitlines())
    except FileNotFoundError:
        sys.stderr.write(f"Warning: macro file not found: {path}\n")
        return {}


# ---------------------------------------------------------------------------
# GAS conditional directive evaluator
# ---------------------------------------------------------------------------

def eval_gas_cond(cond: str, syms: dict) -> "bool | None":
    """
    Evaluate a GAS .if / .elseif condition string.

    Handles:
      - Arithmetic / comparison expressions with numeric literals and known syms
      - GAS logical operators: &&  →  and,  ||  →  or,  !expr  →  not expr
    Returns True, False, or None (unresolvable — treat as False).
    """
    expr = cond.strip()
    # Convert GAS logical operators to Python equivalents.
    # Replace && and || first, then lone ! (not followed by =).
    expr = expr.replace('&&', ' and ')
    expr = expr.replace('||', ' or ')
    expr = re.sub(r'!(?!=)', ' not ', expr)
    val = safe_eval(expr, syms)
    if val is not None:
        return bool(val)
    return None


def apply_conditionals(lines: list, syms: dict) -> list:
    """
    Process GAS .if / .ifnb / .ifb / .ifdef / .ifndef / .elseif / .else / .endif
    blocks in a list of already-substituted lines.

    Selects the appropriate branch and returns only those lines.
    Handles nesting correctly.
    """
    result = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()

        # ---- start of a conditional block ----
        cond_kind = None
        cond_expr = ''

        m = re.match(r'^\.if\b\s*(.*)', stripped)
        if m:
            cond_kind = 'if'
            cond_expr = m.group(1).strip()

        if cond_kind is None:
            m = re.match(r'^\.ifnb\b\s*(.*)', stripped)
            if m:
                cond_kind = 'ifnb'
                cond_expr = m.group(1).strip()

        if cond_kind is None:
            m = re.match(r'^\.ifb\b\s*(.*)', stripped)
            if m:
                cond_kind = 'ifb'
                cond_expr = m.group(1).strip()

        if cond_kind is None:
            m = re.match(r'^\.ifdef\b\s*(.*)', stripped)
            if m:
                cond_kind = 'ifdef'
                cond_expr = m.group(1).strip()

        if cond_kind is None:
            m = re.match(r'^\.ifndef\b\s*(.*)', stripped)
            if m:
                cond_kind = 'ifndef'
                cond_expr = m.group(1).strip()

        if cond_kind is not None:
            # Collect all branches: list of (kind, expr, [body_lines])
            # kind: 'if'/'ifnb'/'ifb'/'ifdef'/'ifndef' for first/elseif,
            #       None for else
            branches = []  # [(cond_kind, cond_expr, [lines])]
            cur_kind = cond_kind
            cur_expr = cond_expr
            cur_body = []
            depth = 1
            i += 1

            while i < len(lines):
                bl = lines[i].strip()
                if re.match(r'^\.if(?:nb|b|def|ndef)?\b', bl):
                    depth += 1
                    cur_body.append(lines[i])
                elif depth == 1 and re.match(r'^\.elseif\b', bl):
                    branches.append((cur_kind, cur_expr, cur_body))
                    m2 = re.match(r'^\.elseif\b\s*(.*)', bl)
                    cur_kind = 'if'
                    cur_expr = m2.group(1).strip() if m2 else ''
                    cur_body = []
                elif depth == 1 and re.match(r'^\.else\b', bl):
                    branches.append((cur_kind, cur_expr, cur_body))
                    cur_kind = None  # else
                    cur_expr = ''
                    cur_body = []
                elif re.match(r'^\.endif\b', bl):
                    depth -= 1
                    if depth == 0:
                        break
                    cur_body.append(lines[i])
                else:
                    cur_body.append(lines[i])
                i += 1

            branches.append((cur_kind, cur_expr, cur_body))

            # Evaluate each branch condition until one is True
            selected = None
            for bkind, bexpr, blines in branches:
                if bkind is None:  # .else branch
                    selected = blines
                    break
                matched = _eval_branch(bkind, bexpr, syms)
                if matched is True:
                    selected = blines
                    break
                # matched is False or None → try next branch
                # (treat None/unknown as False for safety)

            if selected is not None:
                result.extend(apply_conditionals(selected, syms))
            # (if no branch matched and no .else, emit nothing — correct)
            i += 1  # advance past .endif line (we broke before incrementing)
            continue

        # ---- not a conditional directive ----
        result.append(lines[i])
        i += 1

    return result


def _eval_branch(kind: str, expr: str, syms: dict) -> "bool | None":
    """Evaluate a single branch condition."""
    if kind == 'if':
        return eval_gas_cond(expr, syms)
    elif kind == 'ifnb':
        # .ifnb: True if expr (after substitution) is NOT blank
        return bool(expr.strip())
    elif kind == 'ifb':
        # .ifb: True if expr (after substitution) IS blank
        return not bool(expr.strip())
    elif kind == 'ifdef':
        sym = expr.strip()
        return sym in syms
    elif kind == 'ifndef':
        sym = expr.strip()
        return sym not in syms
    return None


# ---------------------------------------------------------------------------
# Argument splitter (respects parentheses)
# ---------------------------------------------------------------------------

def split_args(s: str) -> list:
    """Split comma-separated arguments, respecting parentheses."""
    args = []
    depth = 0
    buf = []
    for c in s:
        if c == '(':
            depth += 1
            buf.append(c)
        elif c == ')':
            depth -= 1
            buf.append(c)
        elif c == ',' and depth == 0:
            args.append(''.join(buf).strip())
            buf = []
        else:
            buf.append(c)
    if buf or args:
        args.append(''.join(buf).strip())
    return [a for a in args if a != ''] if args else []


def split_args_compat(args_str: str, n_params: int) -> list:
    """
    Like split_args(), but also handles ARM GAS's space-as-separator quirk.

    The setstatchanger macro in battle_script.inc writes:
        setbyte sSTATCHANGER \\stat | \\stages << 4 | \\down << 7
    with a SPACE between the ptr arg and the value expression — no comma.
    After C-preprocessing, sSTATCHANGER → 'gBattleScripting + 0x1A', so the
    expanded line is:
        setbyte gBattleScripting + 0x1A 1 | 2 << 4 | 0 << 7

    When there's no comma but the macro expects 2 args, we find the last space
    where the preceding token ends with an alphanumeric/underscore character
    AND the following token starts with a decimal digit.  That space is the
    implicit arg separator, matching what ARM GAS does natively.
    """
    args = split_args(args_str)
    if len(args) >= n_params or ',' in args_str or n_params != 2:
        return args

    # Scan for the split point: last space where:
    #   - the preceding non-space char is alphanumeric, _, or )
    #   - the following non-space char is a decimal digit OR an uppercase
    #     letter (i.e. starts a new numeric or identifier token).
    #
    # This covers two ARM GAS space-as-separator patterns:
    #   setbyte gBattleScripting + 0x1A 1 | ...   (digit → digit)
    #   call_if_not_defeated 362 RocketHideout_*   (digit → uppercase ident)
    best_split = -1
    for i, c in enumerate(args_str):
        if c != ' ':
            continue
        # Walk back over extra spaces
        j = i - 1
        while j >= 0 and args_str[j] == ' ':
            j -= 1
        if j < 0 or not (args_str[j].isalnum() or args_str[j] in '_)'):
            continue
        # Walk forward over extra spaces
        k = i + 1
        while k < len(args_str) and args_str[k] == ' ':
            k += 1
        if k < len(args_str) and (args_str[k].isdigit() or args_str[k].isupper()):
            best_split = i  # take the last qualifying position

    if best_split >= 0:
        return [args_str[:best_split].strip(), args_str[best_split + 1:].strip()]
    return args


# ---------------------------------------------------------------------------
# Line expander (macro invocations → primitive directives)
# ---------------------------------------------------------------------------

def expand_line(line: str, macros: dict, syms: dict, depth=0) -> list:
    """
    Expand a line if it's a macro invocation.
    Returns list of primitive lines (.byte/.2byte/.4byte/.set/label/etc.)
    """
    if depth > 20:
        return [line]  # prevent infinite recursion

    stripped = line.strip()

    # Skip empty / comments / known directives that aren't macro invocations
    if not stripped or stripped.startswith('@') or stripped.startswith('//'):
        return []

    # GAS .set / .equiv / .equ
    if stripped.startswith('.set') or stripped.startswith('.equiv') or stripped.startswith('.equ'):
        return [stripped]

    # Known assembler directives: pass through
    for pfx in ('.byte', '.2byte', '.hword', '.4byte', '.word', '.8byte',
                '.section', '.global', '.align', '.balign', '.ltorg',
                '.include', '.string', '.asciz', '.ascii', '.space', '.fill',
                '.macro', '.endm', '.thumb', '.arm', '.thumb_func',
                '.type', '.size', '.weak', '.hidden', '.end'):
        if stripped.startswith(pfx):
            return [stripped]

    # Label definitions: Name:: or Name:
    if re.match(r'^(?:[A-Za-z_]\w*|\.[A-Za-z_][\w\.]*)\s*::?\s*$', stripped):
        return [stripped]

    # Line might be "Label:   rest" — split label from rest
    m = re.match(r'^((?:[A-Za-z_]\w*|\.[A-Za-z_][\w\.]*)\s*::?)\s+(.*)', stripped)
    if m:
        label_part = re.sub(r'\s+(?=::?$)', '', m.group(1))
        rest_part = m.group(2)
        expanded_rest = expand_line('\t' + rest_part, macros, syms, depth)
        return [label_part] + expanded_rest

    # Check if this line is a macro invocation
    parts = stripped.split(None, 1)
    if not parts:
        return []
    name = parts[0]
    args_str = parts[1] if len(parts) > 1 else ''

    if name in macros:
        n_params = len(macros[name].params)
        args = split_args_compat(args_str, n_params)
        body = macros[name].expand(args)
        # Apply .if/.ifnb/.else/.endif conditionals on the substituted body
        # BEFORE recursing into individual lines, so that:
        #   - .ifnb blocks select the correct branch for trycompare/goto_if_*
        #   - .if EXPR blocks evaluate numeric conditions (compare, stringvar)
        body = apply_conditionals(body, syms)
        if any(re.match(r'^\s*\.macro\b', bline) for bline in body):
            macros.update(load_macros_from_lines(body))
            return []
        result = []
        for bline in body:
            result.extend(expand_line(bline, macros, syms, depth + 1))
        return result

    # Unknown: pass through as-is (might be external symbol or ignored directive)
    return [stripped]


# ---------------------------------------------------------------------------
# Assembler
# ---------------------------------------------------------------------------

class Assembler:
    def __init__(self, pokefiredir: str):
        self.pokefiredir = pokefiredir
        self.macros: dict = {}
        self.syms: dict = {}   # .set symbols: name → int
        self.data = bytearray()
        self.labels: dict = {}  # name → byte offset in data
        self.relocs: list = []  # (offset, sym_name, addend)
        self.global_labels: set = set()

    def add_set(self, name: str, val: int):
        self.syms[name] = val

    def _parse_c_enums(self, all_lines: list):
        """
        Parse C 'enum { A, B=5, C }' declarations from the preprocessed text
        and add each member's numeric value to self.syms.

        gcc -E expands #include guards and #define macros but leaves enum
        bodies as plain C text.  We need to evaluate them ourselves so that
        assembly references like '.2byte HEAL_LOCATION_PALLET_TOWN' resolve
        to the correct integer (1 in this case).

        Handles:
          - Sequential members (A=0, B=1, C=2 ...)
          - Explicit values (B = 5)
          - Values that reference earlier enum members or numeric expressions
        """
        in_enum = False
        current_val = 0
        brace_depth = 0

        for line in all_lines:
            stripped = line.strip()
            if not stripped or stripped.startswith('@') or stripped.startswith('//'):
                continue

            if not in_enum:
                # Look for: enum { ...  or  typedef enum { ...  or  enum Name {
                if re.match(r'^(?:typedef\s+)?enum\b', stripped) and '{' in stripped:
                    in_enum = True
                    brace_depth = stripped.count('{') - stripped.count('}')
                    current_val = 0
                    # Parse any members on the same line as the opening brace
                    body = stripped[stripped.index('{') + 1:]
                    self._parse_enum_body_line(body, current_val)
                    # Recount current_val based on what we parsed
                    # (simpler: just reparse the body to update current_val)
                    current_val = self._enum_body_update(body, current_val)
                    if brace_depth <= 0:
                        in_enum = False
                continue

            # Inside an enum body
            brace_depth += stripped.count('{') - stripped.count('}')
            if brace_depth <= 0:
                in_enum = False
                # Parse any remaining members before the closing brace
                before_close = stripped[:stripped.rindex('}')]
                current_val = self._enum_body_update(before_close, current_val)
            else:
                current_val = self._enum_body_update(stripped, current_val)

    def _enum_body_update(self, text: str, current_val: int) -> int:
        """
        Parse one line of enum body text, update self.syms, return next value.
        """
        # Remove C-style comments
        text = re.sub(r'/\*.*?\*/', '', text)
        text = re.sub(r'//.*', '', text)
        for token in text.split(','):
            token = token.strip().rstrip('}').strip()
            if not token:
                continue
            m = re.match(r'^([A-Za-z_]\w*)\s*=\s*(.+)', token)
            if m:
                name = m.group(1)
                val_str = m.group(2).strip()
                val = safe_eval(val_str, self.syms)
                if val is not None:
                    self.syms[name] = val
                    current_val = val + 1
                else:
                    # Expression uses unknown symbols; skip but still advance
                    current_val += 1
            else:
                m2 = re.match(r'^([A-Za-z_]\w*)', token)
                if m2:
                    name = m2.group(1)
                    self.syms[name] = current_val
                    current_val += 1
        return current_val

    def _parse_enum_body_line(self, text: str, current_val: int):
        """Wrapper — only called for the opening brace line."""
        self._enum_body_update(text, current_val)

    def _inline_file_raw(self, path: str, search_dirs: list, seen: set) -> list:
        """
        Recursively read a file and inline all GAS .include directives.
        Returns a flat list of raw text lines (no .include lines remain).
        'seen' is a set of resolved paths to prevent circular includes.
        """
        if path in seen:
            return []
        seen.add(path)
        try:
            with open(path, errors='replace') as f:
                lines = f.read().splitlines()
        except Exception as e:
            sys.stderr.write(f"Warning: could not read {path}: {e}\n")
            return []

        result = []
        for line in lines:
            stripped = line.strip()
            m = re.match(r'^\.include\s+"([^"]+)"', stripped)
            if m:
                inc_path = m.group(1)
                for d in search_dirs:
                    candidate = os.path.join(d, inc_path)
                    if os.path.exists(candidate):
                        result.extend(
                            self._inline_file_raw(candidate, search_dirs, seen)
                        )
                        break
                # If not found: silently skip (will be warned later if needed)
            else:
                result.append(line)
        return result

    def _preprocess(self, asm_file: str) -> list:
        """
        Full GBA-style preprocessing pipeline:

          1. Recursively inline all .include files into one combined document.
          2. Run the combined text through the upstream 'preproc' tool to
             expand .string "..." directives to .byte sequences using the
             Pokemon charmap (same as the GBA Makefile's PREPROC step).
          3. Run the result through gcc -E -P to expand all C #define
             constants (VAR_RESULT, FLAG_*, TRAINER_*, etc.) that appear
             as arguments throughout the data.

        This mirrors the GBA Makefile pipeline:
          preproc | cpp | preproc -ie | as
        All in a single function call so that every symbol is resolved
        before the assembler sees the text.
        """
        search_dirs = [self.pokefiredir]
        preproc_bin = os.path.join(self.pokefiredir, 'tools/preproc/preproc')
        charmap = os.path.join(self.pokefiredir, 'charmap.txt')

        # Step 1: inline all .include files
        raw_lines = self._inline_file_raw(asm_file, search_dirs, seen=set())
        combined = '\n'.join(raw_lines) + '\n'

        # Step 2: run preproc to expand .string → .byte
        # preproc requires a file with a .s extension; write a temp file.
        if os.path.isfile(preproc_bin) and os.path.isfile(charmap):
            import tempfile
            with tempfile.NamedTemporaryFile(mode='w', suffix='.s',
                                             delete=False,
                                             encoding='utf-8',
                                             errors='replace') as tmp:
                tmp.write(combined)
                tmp_path = tmp.name
            try:
                r = subprocess.run(
                    [preproc_bin, tmp_path, charmap],
                    capture_output=True, text=True
                )
                if r.returncode == 0:
                    combined = r.stdout
                else:
                    sys.stderr.write(
                        f"Warning: preproc failed for {asm_file}:\n"
                        f"{r.stderr[:300]}\n"
                        "Continuing without .string → .byte expansion.\n"
                    )
            except Exception as e:
                sys.stderr.write(f"Warning: preproc error: {e}\n")
            finally:
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass

        # Step 3: gcc -E to expand C #define constants
        cmd = [
            'gcc', '-E', '-P', '-x', 'assembler-with-cpp',
            '-I', os.path.join(self.pokefiredir, 'include'),
            '-include', os.path.join(self.pokefiredir, 'include/constants/event_object_movement.h'),
            '-',
        ]
        try:
            r = subprocess.run(cmd, input=combined,
                               capture_output=True, text=True, check=True)
            return r.stdout.splitlines()
        except subprocess.CalledProcessError as e:
            sys.stderr.write(f"gcc -E failed for {asm_file}:\n{e.stderr[:400]}\n")
            sys.exit(1)

    def _process_file(self, asm_file: str):
        """
        Preprocess a .s file (with full include inlining and charmap
        expansion) and assemble its contents into self.data.
        """
        # Full pipeline: inline + preproc + gcc -E
        all_lines = self._preprocess(asm_file)

        # Parse C enum declarations from the preprocessed output.
        # gcc -E expands #include <...> but leaves 'enum { A, B=5, C }' as-is.
        # We evaluate enum values and add them to self.syms so that references
        # like '.2byte HEAL_LOCATION_PALLET_TOWN' resolve to the correct integer.
        self._parse_c_enums(all_lines)

        # Load macros from the combined, fully-preprocessed result.
        # Because gcc -E has run, C #define constants inside macro bodies
        # are already expanded to their numeric values.
        new_macros = load_macros_from_lines(all_lines)
        self.macros.update(new_macros)

        # Parse .set / NAME=VALUE directives from the preprocessed stream,
        # but ONLY at the top level (not inside .macro/.endm blocks).
        in_macro = 0
        for line in all_lines:
            stripped = line.strip()
            if re.match(r'^\.macro\b', stripped):
                in_macro += 1
                continue
            if re.match(r'^\.endm\b', stripped):
                in_macro = max(0, in_macro - 1)
                continue
            if in_macro > 0:
                continue
            m = re.match(r'^(?:\.set|\.equiv|\.equ)\s+(\w+)\s*,\s*(.+)', stripped)
            if m:
                val = safe_eval(m.group(2), self.syms)
                if val is not None:
                    self.syms[m.group(1)] = val
                continue
            m2 = re.match(r'^(\w+)\s*=\s*(.+)', stripped)
            if m2 and not stripped.startswith('.'):
                val = safe_eval(m2.group(2), self.syms)
                if val is not None:
                    self.syms[m2.group(1)] = val

        # Expand all macro invocations to primitive directives.
        # Skip lines inside .macro/.endm blocks — those are macro bodies and
        # will be expanded when the macro is later invoked, not now.
        # .include lines are gone (already inlined by _inline_file_raw).
        in_macro = 0
        primitives = []
        for line in all_lines:
            stripped = line.strip()
            if not stripped:
                continue
            # Strip GAS/ARM line comments: @ ... and // ...
            # Also handle ';' which preproc uses as a statement separator
            # after label definitions (e.g. "Label: ; .global Label").
            # For our purposes, treat everything from ';' onwards as a
            # comment on the current logical line.
            if ';' in stripped:
                stripped = stripped[:stripped.index(';')].strip()
                if not stripped:
                    continue
            if '@' in stripped:
                stripped = stripped[:stripped.index('@')].strip()
                if not stripped:
                    continue
            if '//' in stripped:
                stripped = stripped[:stripped.index('//')].strip()
                if not stripped:
                    continue
            if stripped.startswith('@') or stripped.startswith('//'):
                continue
            # Track .macro/.endm nesting depth to skip macro bodies
            if re.match(r'^\.macro\b', stripped):
                in_macro += 1
                continue  # don't emit .macro lines into the primitive stream
            if re.match(r'^\.endm\b', stripped):
                in_macro = max(0, in_macro - 1)
                continue
            if in_macro > 0:
                continue  # skip macro body lines
            # .include lines are inlined; skip any stragglers
            if stripped.startswith('.include'):
                continue
            primitives.extend(expand_line(stripped, self.macros, self.syms))

        # Sub-pass A: collect label → byte-offset mappings
        self._pass1(primitives)

        # Sub-pass B: emit bytes and record relocations
        self._pass2(primitives)

    def _pass1(self, lines: list):
        """Collect label → offset mappings (simulation pass)."""
        offset = len(self.data)  # start at current data end

        for line in lines:
            stripped = line.strip()
            if not stripped:
                continue

            # Label definitions
            m = re.match(r'^((?:[A-Za-z_]\w*|\.[A-Za-z_][\w\.]*))\s*(::?)\s*$', stripped)
            if m:
                self.labels[m.group(1)] = offset
                continue

            # .global
            m = re.match(r'^\.global\s+(\w+)', stripped)
            if m:
                self.global_labels.add(m.group(1))
                continue

            # .set / .equiv
            m = re.match(r'^(?:\.set|\.equiv|\.equ)\s+(\w+)\s*,\s*(.+)', stripped)
            if m:
                val = safe_eval(m.group(2), self.syms)
                if val is not None:
                    self.syms[m.group(1)] = val
                continue

            # NAME = VALUE (GAS alternative .set syntax from inc files)
            m = re.match(r'^(\w+)\s*=\s*(.+)', stripped)
            if m and not stripped.startswith('.'):
                val = safe_eval(m.group(2), self.syms)
                if val is not None:
                    self.syms[m.group(1)] = val
                continue

            # .align N → align offset to 2^N
            m = re.match(r'^\.(?:align|balign)\s+(\d+)', stripped)
            if m:
                align = int(m.group(1))
                if stripped.startswith('.align'):
                    align = 1 << align  # .align N → 2^N bytes
                mask = align - 1
                if offset & mask:
                    offset = (offset + mask) & ~mask
                continue

            # .byte
            m = re.match(r'^\.byte\s+(.+)', stripped)
            if m:
                args = split_args(m.group(1))
                offset += len(args)
                continue

            # .2byte / .hword
            m = re.match(r'^\.(?:2byte|hword)\s+(.+)', stripped)
            if m:
                args = split_args(m.group(1))
                offset += 2 * len(args)
                continue

            # .4byte / .word
            m = re.match(r'^\.(?:4byte|word)\s+(.+)', stripped)
            if m:
                args = split_args(m.group(1))
                offset += 4 * len(args)
                continue

            # .space N
            m = re.match(r'^\.space\s+(\d+)', stripped)
            if m:
                offset += int(m.group(1))
                continue

            # .section / other directives: skip
            # Macro invocations in pass1 should have been expanded already

    def _pass2(self, lines: list):
        """Emit bytes into self.data and record relocations."""
        base = len(self.data)  # starting offset for this file's data

        for line in lines:
            stripped = line.strip()
            if not stripped:
                continue

            # Label definitions
            m = re.match(r'^((?:[A-Za-z_]\w*|\.[A-Za-z_][\w\.]*))\s*(::?)\s*$', stripped)
            if m:
                # Label offset already collected in pass1; nothing to emit
                continue

            # .global
            if stripped.startswith('.global'):
                continue

            # .set / .equiv / NAME=
            if re.match(r'^(?:\.set|\.equiv|\.equ)\s', stripped):
                continue
            if re.match(r'^\w+\s*=\s*', stripped) and not stripped.startswith('.'):
                continue

            # .align
            m = re.match(r'^\.(?:align|balign)\s+(\d+)', stripped)
            if m:
                align = int(m.group(1))
                if stripped.startswith('.align'):
                    align = 1 << align
                mask = align - 1
                cur = len(self.data)
                if cur & mask:
                    pad = align - (cur & mask)
                    self.data.extend(b'\x00' * pad)
                continue

            # .space N
            m = re.match(r'^\.space\s+(\d+)', stripped)
            if m:
                self.data.extend(b'\x00' * int(m.group(1)))
                continue

            # .byte
            m = re.match(r'^\.byte\s+(.+)', stripped)
            if m:
                for arg in split_args(m.group(1)):
                    val = safe_eval(arg.strip(), self.syms | self.labels)
                    if val is None:
                        sys.stderr.write(f"Warning: .byte: unresolved '{arg}', using 0\n")
                        val = 0
                    self.data.append(val & 0xFF)
                continue

            # .2byte / .hword
            m = re.match(r'^\.(?:2byte|hword)\s+(.+)', stripped)
            if m:
                for arg in split_args(m.group(1)):
                    val = safe_eval(arg.strip(), self.syms | self.labels)
                    if val is None:
                        sys.stderr.write(f"Warning: .2byte: unresolved '{arg}', using 0\n")
                        val = 0
                    val &= 0xFFFF
                    self.data.append(val & 0xFF)
                    self.data.append((val >> 8) & 0xFF)
                continue

            # .4byte / .word
            m = re.match(r'^\.(?:4byte|word)\s+(.+)', stripped)
            if m:
                for arg in split_args(m.group(1)):
                    kind, *rest = parse_4byte(arg.strip(), self.syms)
                    if kind == 'num':
                        val = rest[0] & 0xFFFFFFFF
                        self.data.extend([
                            val & 0xFF,
                            (val >> 8) & 0xFF,
                            (val >> 16) & 0xFF,
                            (val >> 24) & 0xFF,
                        ])
                    else:
                        sym_name, addend = rest
                        offset = len(self.data)
                        self.relocs.append((offset, sym_name, addend))
                        # Emit 4 zero bytes as placeholder
                        self.data.extend(b'\x00\x00\x00\x00')
                continue

            # .section / other directives: skip
            if stripped.startswith('.'):
                continue

            # Anything else that got through expansion: warn
            # (could be unknown macros or bare identifiers)

    def assemble(self, asm_files: list):
        """Assemble all given .s files in order."""
        for f in asm_files:
            self._process_file(f)


# ---------------------------------------------------------------------------
# C code emitter
# ---------------------------------------------------------------------------

def emit_c(asm: Assembler, data_name: str, prefix: str, patch_fn: str,
           output_file: str, asm_files: list, exclude_labels: set = None):
    """
    Emit the C source file.

    data_name       — name of the raw data array (e.g. "pfr_bs_data")
    prefix          — prefix for avoiding collisions (e.g. "pfr_bs")
    patch_fn        — name of the patch function (e.g. "HostPatchBattleScriptPointers")
    exclude_labels  — labels to omit from asm aliases / header (defined elsewhere in C)
    """
    if exclude_labels is None:
        exclude_labels = set()

    lines = []
    add = lines.append

    add("/*")
    add(f" * {os.path.basename(output_file)}")
    add(" * Auto-generated by tools/script_assembler.py")
    add(" * DO NOT EDIT — regenerate with:")
    cmd_files = ' '.join(os.path.relpath(f) for f in asm_files)
    add(f" *   python3 tools/script_assembler.py \\")
    add(f" *       --pokedir third_party/pokefirered \\")
    add(f" *       --prefix {prefix} \\")
    add(f" *       --patch-fn {patch_fn} \\")
    add(f" *       --output {os.path.relpath(output_file)} \\")
    add(f" *       {cmd_files}")
    add(" */")
    add("")
    add("#include <string.h>")
    add("#include \"global.h\"")
    add("#include \"host_pointer_codec.h\"")
    add("")

    # Collect all unique external symbols referenced in relocations
    # These are C globals (functions, data arrays) that scripts reference by address.
    # We cannot declare them as "extern u8 sym[]" because they may already be declared
    # as functions in upstream headers (e.g. CalculatePlayerPartyCount).
    # Instead, create GCC asm equate aliases with a mangled name so we can safely
    # declare the alias as u8[] without conflicting with the real C declaration.
    reloc_syms = set()
    for _, sym, _ in asm.relocs:
        if sym not in asm.labels:
            reloc_syms.add(sym)

    if reloc_syms:
        add("/* GCC asm aliases for external C symbols referenced in script .4byte relocations.")
        add(" * Using mangled names avoids type-conflict errors when the symbol is a function.")
        add(" * The aliases are equated to the real symbol in the assembler, so their addresses")
        add(" * are identical to the originals. */")
        add("__asm__(")
        for sym in sorted(reloc_syms):
            mangled = f"{prefix}_ext_{sym}"
            add(f'    ".global {mangled}\\n\\t"')
            add(f'    "{mangled} = {sym}\\n\\t"')
        add(");")
        add("")
        for sym in sorted(reloc_syms):
            mangled = f"{prefix}_ext_{sym}"
            add(f"extern u8 {mangled}[];")
        add("")

    # Emit the raw data array
    total = len(asm.data)
    add(f"/* {total} bytes of assembled script data */")
    add(f"u8 {data_name}[{total}] = {{")

    # Emit bytes in groups of 16, with label markers
    # Build offset→label map for comments
    offset_labels = {}
    for lname, off in asm.labels.items():
        offset_labels.setdefault(off, []).append(lname)

    i = 0
    while i < total:
        chunk_end = min(i + 16, total)
        hex_bytes = ', '.join(f'0x{b:02x}' for b in asm.data[i:chunk_end])

        # Add label comments at this offset
        comment = ''
        for label in offset_labels.get(i, []):
            comment += f' /* {label} */'

        add(f"    {hex_bytes},{comment}")
        i = chunk_end

    add("};")
    add("")

    # Emit GCC inline-asm symbol aliases for all labels
    # Sort by offset for readability
    sorted_labels = [(n, o) for n, o in sorted(asm.labels.items(), key=lambda kv: kv[1])
                     if n not in exclude_labels and not n.startswith('.')]

    add("/* Symbol aliases: each script label points into the data array. */")
    add("/* These satisfy 'extern const u8 LabelName[]' in upstream headers. */")

    # GCC asm volatile block — one big block for all aliases
    add("__asm__(")
    for lname, offset in sorted_labels:
        # Emit .global + alias assignment
        add(f'    ".global {lname}\\n\\t"')
        add(f'    "{lname} = {data_name} + {offset}\\n\\t"')
    add(");")
    add("")

    # Emit extern declarations for the labels so C code in this file can use them
    add("/* Forward-declare labels so the patch function can reference them. */")
    for lname, _ in sorted_labels:
        add(f"extern u8 {lname}[];")
    add("")

    # Emit static patch helper
    add("/* Patch function: writes encoded 32-bit host pointers into script data. */")
    add("/* Internal script-label references are mirrored into low alias space so */")
    add("/* the original 32-bit script pointer math still works on 64-bit hosts.  */")
    add(f"static void {prefix}_patch4(u8 *dst, const void *target) {{")
    add(f"    HostWritePatchedPointer({data_name}, sizeof({data_name}), dst, target);")
    add("}")
    add("")

    # Emit patch function
    add(f"void {patch_fn}(void) {{")
    for (offset, sym, addend) in asm.relocs:
        if sym in asm.labels:
            ref = sym  # script label, declared as extern u8 sym[]
        else:
            ref = f"{prefix}_ext_{sym}"  # GAS alias for external C symbol
        if addend != 0:
            target = f"((u8 *)({ref}) + {addend})"
        else:
            target = ref
        add(f"    {prefix}_patch4(&{data_name}[{offset}], {target});")
    add("}")
    add("")

    # Write output
    content = '\n'.join(lines) + '\n'
    with open(output_file, 'w') as f:
        f.write(content)

    print(f"Generated {output_file}: {total} bytes, "
          f"{len(asm.labels)} labels, {len(asm.relocs)} relocations.")

    # Write companion .h file so other translation units can reference the labels
    h_file = os.path.splitext(output_file)[0] + '.h'
    guard = os.path.basename(h_file).upper().replace('.', '_').replace('-', '_')
    h_lines = []
    h_lines.append(f"#ifndef {guard}")
    h_lines.append(f"#define {guard}")
    h_lines.append("")
    h_lines.append("/* Auto-generated by tools/script_assembler.py — do not edit. */")
    h_lines.append("")
    h_lines.append("#include \"global.h\"")
    h_lines.append("")
    for lname, _ in sorted_labels:
        h_lines.append(f"extern const u8 {lname}[];")
    h_lines.append("")
    h_lines.append(f"void {patch_fn}(void);")
    h_lines.append("")
    h_lines.append(f"#endif /* {guard} */")
    h_content = '\n'.join(h_lines) + '\n'
    with open(h_file, 'w') as hf:
        hf.write(h_content)
    print(f"Generated {h_file}: {len(sorted_labels)} label declarations.")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('asm_files', nargs='+', metavar='FILE.s',
                        help='Input .s files to assemble')
    parser.add_argument('--pokedir', default='third_party/pokefirered',
                        help='Root of pokefirered source tree')
    parser.add_argument('--prefix', default='pfr_bs',
                        help='Prefix for internal names (e.g. pfr_bs)')
    parser.add_argument('--patch-fn', default='HostPatchBattleScriptPointers',
                        dest='patch_fn',
                        help='Name of the generated patch function')
    parser.add_argument('--output', '-o', default='-',
                        help='Output .c file (default: stdout, use - for stdout)')
    parser.add_argument('--exclude-label', action='append', default=[],
                        dest='exclude_labels',
                        help='Label to omit from asm aliases / header (defined in C elsewhere)')
    args = parser.parse_args()

    # Resolve paths
    pokefiredir = os.path.abspath(args.pokedir)
    asm_files = [os.path.abspath(f) for f in args.asm_files]
    output_file = args.output

    # Create assembler and run
    asm = Assembler(pokefiredir)

    # Add common symbols that appear in script files
    asm.add_set('NULL', 0)
    asm.add_set('FALSE', 0)
    asm.add_set('TRUE', 1)

    # STR_VAR_* are charmap entries (FD 02 / FD 03 / FD 04), not C #defines.
    # The stringvar macro uses '.if \id == STR_VAR_1' to map them to indices
    # 0/1/2.  Assign unique values so the .if conditions evaluate correctly.
    asm.add_set('STR_VAR_1', 0xFD02)
    asm.add_set('STR_VAR_2', 0xFD03)
    asm.add_set('STR_VAR_3', 0xFD04)

    asm.assemble(asm_files)

    # Determine data array name
    data_name = f"{args.prefix}_data"

    if output_file == '-':
        import tempfile
        with tempfile.NamedTemporaryFile(mode='w', suffix='.c',
                                         delete=False) as tmp:
            tmp_path = tmp.name
        excl = set(args.exclude_labels)
        emit_c(asm, data_name, args.prefix, args.patch_fn, tmp_path, asm_files, excl)
        with open(tmp_path) as f:
            sys.stdout.write(f.read())
        os.unlink(tmp_path)
    else:
        if not os.path.isabs(output_file):
            output_file = os.path.abspath(output_file)
        excl = set(args.exclude_labels)
        emit_c(asm, data_name, args.prefix, args.patch_fn, output_file,
               asm_files, excl)


if __name__ == '__main__':
    main()
