#!/usr/bin/env python3
"""
sound_data_to_c.py

Converts GBA assembly sound data from the pokefirered decomp into native C
source files for the pokefirered-native 64-bit build.

Generates:
  src/upstream_sound_data.c    — C source with voice groups, song data, tables
  src/upstream_sound_samples.S — GAS assembly with .incbin for PCM samples

Run from repo root:
  python3 tools/sound_data_to_c.py
"""

import argparse
import os
import re
import struct
import sys
from collections import OrderedDict


# ---------------------------------------------------------------------------
# MPlayDef.s constant parser
# ---------------------------------------------------------------------------

def parse_mplaydef(path):
    """Parse MPlayDef.s and return a dict of symbol -> int value."""
    symbols = {}

    with open(path) as f:
        for line in f:
            line = line.split('@')[0].strip()
            m = re.match(r'\.equ\s+(\w+)\s*,\s*(.+)', line)
            if m:
                name = m.group(1)
                expr = m.group(2).strip()
                val = _eval_expr(expr, symbols)
                if val is not None:
                    symbols[name] = val
    return symbols


def _eval_expr(expr, symbols):
    """Evaluate a simple expression with +, -, *, / and hex/decimal literals."""
    try:
        # Tokenize: split into tokens preserving operators
        tokens = re.findall(r'[A-Za-z_]\w*|0x[0-9A-Fa-f]+|\d+|[+\-*/()&|]', expr)
        result_tokens = []
        for tok in tokens:
            if re.match(r'^[A-Za-z_]\w*$', tok):
                if tok in symbols:
                    result_tokens.append(str(symbols[tok]))
                else:
                    return None
            elif re.match(r'^0x[0-9A-Fa-f]+$', tok, re.IGNORECASE):
                result_tokens.append(str(int(tok, 16)))
            else:
                result_tokens.append(tok)
        # Replace / with // for integer division
        eval_str = ' '.join(result_tokens).replace('/', '//')
        # Avoid double-slash from //
        eval_str = eval_str.replace('////', '//')
        return int(eval(eval_str))
    except Exception:
        return None


def eval_song_expr(expr, symbols):
    """Evaluate a song expression, returning an integer.

    Handles arithmetic with song-local symbols and MPlayDef constants.
    All division is integer division.
    """
    expr = expr.strip()
    # Try direct lookup first
    if expr in symbols:
        return symbols[expr]
    # Try hex literal
    if re.match(r'^0x[0-9A-Fa-f]+$', expr, re.IGNORECASE):
        return int(expr, 16)
    # Try decimal
    if re.match(r'^-?\d+$', expr):
        return int(expr)

    # Tokenize
    tokens = re.findall(r'[A-Za-z_]\w*|0x[0-9A-Fa-f]+|\d+|[+\-*/()&|]', expr)
    result_tokens = []
    for tok in tokens:
        if re.match(r'^[A-Za-z_]\w*$', tok):
            if tok in symbols:
                result_tokens.append(str(symbols[tok]))
            else:
                raise ValueError(f"Unknown symbol '{tok}' in expression '{expr}'")
        elif re.match(r'^0x[0-9A-Fa-f]+$', tok, re.IGNORECASE):
            result_tokens.append(str(int(tok, 16)))
        else:
            result_tokens.append(tok)

    eval_str = ' '.join(result_tokens).replace('/', '//')
    eval_str = eval_str.replace('////', '//')
    return int(eval(eval_str))


# ---------------------------------------------------------------------------
# Direct sound data parser
# ---------------------------------------------------------------------------

def parse_direct_sound_data(path):
    """Parse direct_sound_data.inc, return ordered list of (symbol_name, incbin_path) tuples."""
    samples = []
    current_label = None
    with open(path) as f:
        for line in f:
            stripped = line.split('@')[0].strip()
            m = re.match(r'^(\w+)::?\s*$', stripped)
            if m:
                current_label = m.group(1)
                continue
            if current_label and '.incbin' in stripped:
                m2 = re.search(r'\.incbin\s+"sound/(.+?)"', stripped)
                if m2:
                    samples.append((current_label, m2.group(1)))
                current_label = None
    return samples


# ---------------------------------------------------------------------------
# Programmable wave data parser
# ---------------------------------------------------------------------------

def parse_programmable_wave_data(path):
    """Parse programmable_wave_data.inc, return ordered list of (symbol_name, incbin_path) tuples."""
    samples = []
    current_label = None
    with open(path) as f:
        for line in f:
            stripped = line.split('@')[0].strip()
            m = re.match(r'^(\w+)::?\s*$', stripped)
            if m:
                current_label = m.group(1)
                continue
            if current_label and '.incbin' in stripped:
                m2 = re.search(r'\.incbin\s+"sound/(.+?)"', stripped)
                if m2:
                    samples.append((current_label, m2.group(1)))
                current_label = None
    return samples


# ---------------------------------------------------------------------------
# Keysplit table parser
# ---------------------------------------------------------------------------

def parse_keysplit_tables(path):
    """Parse keysplit_tables.inc, return list of (name, offset, data_bytes)."""
    tables = []
    current_name = None
    current_offset = None
    current_data = []

    with open(path) as f:
        for line in f:
            line = line.split('@')[0].strip()
            if not line:
                continue

            m = re.match(r'\.set\s+(\w+)\s*,\s*\.\s*-\s*(\d+)', line)
            if m:
                # Save previous table
                if current_name is not None:
                    tables.append((current_name, current_offset, current_data))
                current_name = m.group(1)
                current_offset = int(m.group(2))
                current_data = []
                continue

            m = re.match(r'\.byte\s+(\d+)', line)
            if m and current_name is not None:
                current_data.append(int(m.group(1)))

    if current_name is not None:
        tables.append((current_name, current_offset, current_data))

    return tables


# ---------------------------------------------------------------------------
# Voice groups parser
# ---------------------------------------------------------------------------

def parse_voice_groups(path):
    """Parse voice_groups.inc, return OrderedDict of name -> list of voice entries."""
    groups = OrderedDict()
    current_group = None
    current_voices = []

    with open(path) as f:
        for line in f:
            line = line.split('@')[0].strip()
            if not line:
                continue

            # Voice group label
            m = re.match(r'^(\w+)::?\s*$', line)
            if m:
                if current_group is not None:
                    groups[current_group] = current_voices
                current_group = m.group(1)
                current_voices = []
                continue

            if line.startswith('.align'):
                continue

            # Voice macros
            voice = parse_voice_line(line)
            if voice is not None and current_group is not None:
                current_voices.append(voice)

    if current_group is not None:
        groups[current_group] = current_voices

    return groups


def parse_voice_line(line):
    """Parse a single voice macro line, return a dict or None."""
    line = line.strip()

    patterns = [
        (r'voice_directsound\s+(.+)', 'directsound', 0x00),
        (r'voice_directsound_no_resample\s+(.+)', 'directsound', 0x08),
        (r'voice_directsound_alt\s+(.+)', 'directsound', 0x10),
        (r'voice_square_1_alt\s+(.+)', 'square_1', 0x09),
        (r'voice_square_1\s+(.+)', 'square_1', 0x01),
        (r'voice_square_2_alt\s+(.+)', 'square_2', 0x0A),
        (r'voice_square_2\s+(.+)', 'square_2', 0x02),
        (r'voice_programmable_wave_alt\s+(.+)', 'programmable_wave', 0x0B),
        (r'voice_programmable_wave\s+(.+)', 'programmable_wave', 0x03),
        (r'voice_noise_alt\s+(.+)', 'noise', 0x0C),
        (r'voice_noise\s+(.+)', 'noise', 0x04),
        (r'voice_keysplit_all\s+(.+)', 'keysplit_all', 0x80),
        (r'voice_keysplit\s+(.+)', 'keysplit', 0x40),
    ]

    for pat, kind, type_val in patterns:
        m = re.match(pat, line)
        if m:
            args = [a.strip() for a in m.group(1).split(',')]
            return _build_voice(kind, type_val, args)

    return None


def _build_voice(kind, type_val, args):
    """Build a voice dict from parsed arguments."""
    if kind == 'directsound':
        key, pan, sample, attack, decay, sustain, release = args
        pan_val = int(pan)
        pan_sweep = (0x80 | pan_val) if pan_val != 0 else 0
        return {
            'type': type_val,
            'key': int(key),
            'length': 0,
            'pan_sweep': pan_sweep,
            'wav_kind': 'directsound',
            'wav_ref': sample,
            'attack': int(attack),
            'decay': int(decay),
            'sustain': int(sustain),
            'release': int(release),
        }

    elif kind == 'square_1':
        key, pan, sweep, duty, attack, decay, sustain, release = args
        pan_val = int(pan)
        length = (0x80 | pan_val) if pan_val != 0 else 0
        return {
            'type': type_val,
            'key': int(key),
            'length': length,
            'pan_sweep': int(sweep),
            'wav_kind': 'integer',
            'wav_ref': int(duty) & 3,
            'attack': int(attack) & 7,
            'decay': int(decay) & 7,
            'sustain': int(sustain) & 0xF,
            'release': int(release) & 7,
        }

    elif kind == 'square_2':
        key, pan, duty, attack, decay, sustain, release = args
        pan_val = int(pan)
        length = (0x80 | pan_val) if pan_val != 0 else 0
        return {
            'type': type_val,
            'key': int(key),
            'length': length,
            'pan_sweep': 0,
            'wav_kind': 'integer',
            'wav_ref': int(duty) & 3,
            'attack': int(attack) & 7,
            'decay': int(decay) & 7,
            'sustain': int(sustain) & 0xF,
            'release': int(release) & 7,
        }

    elif kind == 'programmable_wave':
        key, pan, wave_ptr, attack, decay, sustain, release = args
        pan_val = int(pan)
        length = (0x80 | pan_val) if pan_val != 0 else 0
        return {
            'type': type_val,
            'key': int(key),
            'length': length,
            'pan_sweep': 0,
            'wav_kind': 'programmable_wave',
            'wav_ref': wave_ptr,
            'attack': int(attack) & 7,
            'decay': int(decay) & 7,
            'sustain': int(sustain) & 0xF,
            'release': int(release) & 7,
        }

    elif kind == 'noise':
        key, pan, period, attack, decay, sustain, release = args
        pan_val = int(pan)
        length = (0x80 | pan_val) if pan_val != 0 else 0
        return {
            'type': type_val,
            'key': int(key),
            'length': length,
            'pan_sweep': 0,
            'wav_kind': 'integer',
            'wav_ref': int(period) & 1,
            'attack': int(attack) & 7,
            'decay': int(decay) & 7,
            'sustain': int(sustain) & 0xF,
            'release': int(release) & 7,
        }

    elif kind == 'keysplit':
        voicegroup_ptr, keysplit_table_ptr = args
        return {
            'type': type_val,
            'key': 0,
            'length': 0,
            'pan_sweep': 0,
            'wav_kind': 'voicegroup',
            'wav_ref': voicegroup_ptr,
            'attack': 0,
            'decay': 0,
            'sustain': 0,
            'release': 0,
            'keysplit_table': keysplit_table_ptr,
        }

    elif kind == 'keysplit_all':
        voicegroup_ptr = args[0]
        return {
            'type': type_val,
            'key': 0,
            'length': 0,
            'pan_sweep': 0,
            'wav_kind': 'voicegroup',
            'wav_ref': voicegroup_ptr,
            'attack': 0,
            'decay': 0,
            'sustain': 0,
            'release': 0,
            'keysplit_table': None,
        }

    return None


# ---------------------------------------------------------------------------
# Song file parser
# ---------------------------------------------------------------------------

def parse_song_file(path, mplaydef_symbols):
    """Parse a song .s file, returning song metadata and track data.

    Returns dict:
        name: song global name
        equs: dict of song-local .equ symbols
        tracks: list of (track_label, byte_data) — byte_data is a bytearray
        header_track_count: int
        header_block_count: int
        header_pri_expr: str
        header_rev_expr: str
        header_grp_expr: str
        track_labels_in_header: list of track label names
    """
    song = {
        'name': None,
        'equs': {},
        'tracks': [],
        'track_labels_in_header': [],
    }

    # Merge mplaydef symbols as base
    all_symbols = dict(mplaydef_symbols)

    lines = []
    with open(path) as f:
        for line in f:
            lines.append(line)

    # First pass: collect .equ definitions
    for line in lines:
        stripped = line.split('@')[0].strip()
        m = re.match(r'\.equ\s+(\w+)\s*,\s*(.+)', stripped)
        if m:
            name = m.group(1)
            expr = m.group(2).strip()
            # Try to evaluate, but some reference voicegroup names which are not numeric
            song['equs'][name] = expr
            # If it's a pure voicegroup reference, don't try to evaluate
            if re.match(r'^voicegroup\d+$', expr):
                all_symbols[name] = expr  # Keep as string
            else:
                val = _eval_expr(expr, {k: v for k, v in all_symbols.items() if isinstance(v, int)})
                if val is not None:
                    all_symbols[name] = val

    # Find global name
    for line in lines:
        stripped = line.split('@')[0].strip()
        m = re.match(r'\.global\s+(\w+)', stripped)
        if m:
            song['name'] = m.group(1)
            break

    if song['name'] is None:
        # Try to find from song header label
        for line in lines:
            stripped = line.split('@')[0].strip()
            m = re.match(r'^(\w+):\s*$', stripped)
            if m and not m.group(1).endswith(('_1', '_2', '_3', '_4', '_5', '_6',
                                               '_7', '_8', '_9', '_10', '_B1', '_B2')):
                # Check if followed by .byte for track count
                song['name'] = m.group(1)

    if song['name'] is None:
        print(f"WARNING: Could not determine song name in {path}", file=sys.stderr)
        return None

    song_name = song['name']

    # Collect all labels and their line indices
    label_lines = {}
    for i, line in enumerate(lines):
        stripped = line.split('@')[0].strip()
        m = re.match(r'^(\w+):\s*$', stripped)
        if m:
            label_lines[m.group(1)] = i

    # Find the song header (the label matching song_name)
    header_line_idx = label_lines.get(song_name)
    if header_line_idx is None:
        print(f"WARNING: Could not find header label '{song_name}' in {path}", file=sys.stderr)
        return None

    # Parse header: it starts with .byte for track count, block count, pri, rev
    # then .word for voice group, then .word for each track
    header_bytes = []
    header_words = []
    for i in range(header_line_idx + 1, len(lines)):
        stripped = lines[i].split('@')[0].strip()
        if not stripped or stripped.startswith('.align') or stripped.startswith('.end'):
            continue
        if stripped.startswith('.byte'):
            byte_args = stripped[5:].strip()
            for arg in byte_args.split(','):
                arg = arg.strip()
                if not arg:
                    continue
                header_bytes.append(arg)
        elif stripped.startswith('.word'):
            word_arg = stripped[5:].strip()
            header_words.append(word_arg)
        elif re.match(r'^\w+:', stripped):
            break

    # header_bytes[0] = track count, [1] = block count, [2] = pri, [3] = rev
    # header_words[0] = voicegroup, [1..N] = track pointers
    if len(header_bytes) < 4:
        print(f"WARNING: Incomplete header in {path}: {header_bytes}", file=sys.stderr)
        return None

    int_syms = {k: v for k, v in all_symbols.items() if isinstance(v, int)}
    try:
        song['header_track_count'] = eval_song_expr(header_bytes[0], int_syms)
    except Exception:
        song['header_track_count'] = 0

    try:
        song['header_block_count'] = eval_song_expr(header_bytes[1], int_syms)
    except Exception:
        song['header_block_count'] = 0

    song['header_pri_expr'] = header_bytes[2]
    song['header_rev_expr'] = header_bytes[3]
    song['header_grp_expr'] = header_words[0] if header_words else 'voicegroup000'
    song['track_labels_in_header'] = header_words[1:]

    # Resolve pri and rev to integers
    try:
        song['header_pri'] = eval_song_expr(header_bytes[2], int_syms)
    except Exception:
        song['header_pri'] = 0
    try:
        song['header_rev'] = eval_song_expr(header_bytes[3], int_syms)
    except Exception:
        song['header_rev'] = 0

    # Resolve voice group name
    grp_expr = song['header_grp_expr']
    if grp_expr in all_symbols and isinstance(all_symbols[grp_expr], str):
        song['header_grp'] = all_symbols[grp_expr]
    elif re.match(r'^voicegroup\d+$', grp_expr):
        song['header_grp'] = grp_expr
    else:
        song['header_grp'] = grp_expr

    # Now parse each track
    # Find track labels (labels referenced in header words)
    track_label_names = []
    for w in song['track_labels_in_header']:
        track_label_names.append(w.strip())

    for track_label in track_label_names:
        track_data = _parse_track(lines, label_lines, track_label, all_symbols, int_syms)
        if track_data is not None:
            song['tracks'].append((track_label, track_data))
        else:
            print(f"WARNING: Could not parse track '{track_label}' in {path}", file=sys.stderr)
            song['tracks'].append((track_label, bytearray()))

    return song


def _parse_track(lines, label_lines, track_label, all_symbols, int_syms):
    """Parse a single track's byte/word data into a bytearray.

    .word directives (after GOTO/PATT/REPT or MEMACC branch) are converted
    to self-relative 32-bit signed offsets.
    """
    start_idx = label_lines.get(track_label)
    if start_idx is None:
        return None

    # First, collect all labels within this track and their byte offsets.
    # We need two passes: first to determine byte offsets of all labels,
    # then to resolve .word references.

    # Determine the end of this track: next label that is NOT a sub-label of this track
    # OR is a label referenced as a header track label or header label.
    # Actually, a track ends at FINE or at the next voicegroup/song header label.
    # But labels within the track (like _B1, _loop etc.) are internal.
    # The simplest approach: parse until we hit another top-level label that's
    # also a track start or the song header, OR we hit .align/.end after FINE.

    # Collect all lines belonging to this track
    track_lines = []
    # Find all track-start labels and the header label
    header_and_track_labels = set()
    for w in all_symbols:
        if isinstance(all_symbols[w], str) and all_symbols[w].startswith('voicegroup'):
            continue
    # We don't have a perfect way, so we'll just collect until we see a label
    # that's another track or the header, or .end, or another .align 2 followed by
    # a label.

    # Strategy: collect lines from start_idx+1.
    # Stop when we encounter a label that is NOT between start_idx and itself
    # that is in the label_lines dict AND is followed by track-count-like data
    # OR when we see .end.
    # For simplicity, stop at:
    # - a label on its own line that appears in label_lines and is not an internal label
    #   (i.e., appears in the header's track list or is the song header itself)
    #
    # Actually, just collect until we see .align or a label that's a header/track boundary.
    # Internal labels within the track are fine.

    # Let's use a simpler heuristic: parse line-by-line, emit bytes,
    # and stop when we emit FINE (0xB1) or hit .end or .align 2 at the top level
    # after at least one FINE.

    byte_data = bytearray()
    label_offsets = {}  # label_name -> byte offset within this track
    word_fixups = []    # list of (byte_offset, target_label)

    label_offsets[track_label] = 0

    # We need to do two passes. First pass: compute offsets. Second: emit bytes.
    # Actually we can do it in one pass with fixups.

    i = start_idx + 1
    found_fine = False
    while i < len(lines):
        raw_line = lines[i]
        stripped = raw_line.split('@')[0].strip()
        i += 1

        if not stripped:
            continue

        # Label?
        m = re.match(r'^(\w+):\s*$', stripped)
        if m:
            lbl = m.group(1)
            label_offsets[lbl] = len(byte_data)
            # Check if this is a boundary label (another track or the song header)
            # If we've already seen FINE and this label doesn't look like an internal label
            if found_fine:
                break
            continue

        if stripped.startswith('.end'):
            break

        if stripped.startswith('.align'):
            # .align in middle of track data: if we've seen FINE, this marks end
            if found_fine:
                break
            # Otherwise skip (alignment padding in track data is not emitted as bytes)
            continue

        if stripped.startswith('.section'):
            continue

        if stripped.startswith('.global'):
            continue

        # .byte directive
        if stripped.startswith('.byte'):
            byte_args_str = stripped[5:].strip()
            byte_args = _split_byte_args(byte_args_str)
            for arg in byte_args:
                arg = arg.strip()
                if not arg:
                    continue
                try:
                    val = eval_song_expr(arg, int_syms)
                    byte_data.append(val & 0xFF)
                    if val == 0xB1:  # FINE
                        found_fine = True
                except Exception as e:
                    print(f"WARNING: Cannot evaluate byte expr '{arg}': {e}", file=sys.stderr)
                    byte_data.append(0)
            continue

        # .word directive (pointer to label)
        if stripped.startswith('.word'):
            word_arg = stripped[5:].strip()
            word_fixups.append((len(byte_data), word_arg))
            # Placeholder 4 bytes
            byte_data.extend(b'\x00\x00\x00\x00')
            continue

    # Now resolve word fixups as self-relative offsets
    for offset, target_label in word_fixups:
        if target_label in label_offsets:
            target_offset = label_offsets[target_label]
            rel_offset = target_offset - offset
            # Pack as signed 32-bit little-endian
            struct.pack_into('<i', byte_data, offset, rel_offset)
        else:
            print(f"WARNING: Unresolved label '{target_label}' in track '{track_label}'",
                  file=sys.stderr)

    return byte_data


def _split_byte_args(s):
    """Split .byte arguments by comma, handling expressions with operators."""
    # Simple comma split works since expressions don't contain commas
    return [x.strip() for x in s.split(',') if x.strip()]


# ---------------------------------------------------------------------------
# Song table parser
# ---------------------------------------------------------------------------

def parse_song_table(path):
    """Parse song_table.inc, return list of (song_name, ms, me)."""
    entries = []
    with open(path) as f:
        for line in f:
            line = line.split('@')[0].strip()
            m = re.match(r'song\s+(\w+)\s*,\s*(\d+)\s*,\s*(\d+)', line)
            if m:
                entries.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return entries


# ---------------------------------------------------------------------------
# Cry table parser
# ---------------------------------------------------------------------------

def parse_cry_tables(path):
    """Parse cry_tables.inc, return (cries_forward, cries_reverse) as lists of sample names."""
    forward = []
    reverse = []
    in_reverse = False

    with open(path) as f:
        for line in f:
            line = line.split('@')[0].strip()
            if 'gCryTable_Reverse' in line:
                in_reverse = True
                continue
            if 'gCryTable::' in line:
                in_reverse = False
                continue

            m = re.match(r'cry_reverse\s+(\w+)', line)
            if m:
                reverse.append(m.group(1))
                continue
            m = re.match(r'cry\s+(\w+)', line)
            if m:
                forward.append(m.group(1))
                continue

    return forward, reverse


# ---------------------------------------------------------------------------
# Code generators
# ---------------------------------------------------------------------------

def generate_samples_asm(direct_sounds, prog_waves, decomp_root, output_path):
    """Generate upstream_sound_samples.S

    direct_sounds and prog_waves are lists of (label_name, incbin_relative_path) tuples.
    """
    with open(output_path, 'w') as f:
        f.write("// Auto-generated by tools/sound_data_to_c.py\n")
        f.write("// Do not edit manually.\n")
        f.write(".section .rodata\n\n")

        for name, rel_path in direct_sounds:
            f.write(f".align 4\n")
            f.write(f".global raw_{name}\n")
            f.write(f"raw_{name}:\n")
            f.write(f".incbin \"{decomp_root}sound/{rel_path}\"\n\n")

        for name, rel_path in prog_waves:
            f.write(f".align 4\n")
            f.write(f".global raw_{name}\n")
            f.write(f"raw_{name}:\n")
            f.write(f".incbin \"{decomp_root}sound/{rel_path}\"\n\n")



def _wav_expr(voice):
    """Generate the wav field expression for a ToneData initializer."""
    kind = voice['wav_kind']
    ref = voice['wav_ref']

    if kind == 'directsound':
        return f"(struct WaveData *)raw_{ref}"
    elif kind == 'programmable_wave':
        return f"(struct WaveData *)raw_{ref}"
    elif kind == 'integer':
        return f"(struct WaveData *)(uintptr_t){ref}"
    elif kind == 'voicegroup':
        return f"(struct WaveData *){ref}"
    else:
        return "(struct WaveData *)0"


def _keysplit_expr(voice):
    """Generate the keySplitTable field expression."""
    kst = voice.get('keysplit_table')
    if kst is None:
        return "NULL"
    return f"(const u8 *){kst}"


def generate_sound_data_c(direct_sounds, prog_waves, keysplit_tables,
                          voice_groups, songs_data, song_table,
                          cries_forward, cries_reverse, output_path):
    """Generate upstream_sound_data.c

    direct_sounds and prog_waves are lists of (label_name, incbin_path) tuples.
    """
    with open(output_path, 'w') as f:
        f.write("// Auto-generated by tools/sound_data_to_c.py\n")
        f.write("// Do not edit manually.\n")
        f.write("#include \"global.h\"\n")
        f.write("#include \"gba/m4a_internal.h\"\n\n")

        # -- Extern declarations for sample data --
        f.write("// ── Extern declarations for .incbin sample data ──\n")
        for name, _ in direct_sounds:
            f.write(f"extern u8 raw_{name}[];\n")
        for name, _ in prog_waves:
            f.write(f"extern u8 raw_{name}[];\n")
        f.write("\n")

        # -- Keysplit tables --
        f.write("// ── Keysplit tables ──\n")
        for tbl_name, offset, data_bytes in keysplit_tables:
            arr = [0] * 128
            for idx, val in enumerate(data_bytes):
                real_idx = offset + idx
                if 0 <= real_idx < 128:
                    arr[real_idx] = val
            f.write(f"const u8 {tbl_name}[128] = {{\n    ")
            for j in range(128):
                f.write(f"{arr[j]}")
                if j < 127:
                    f.write(", ")
                if (j + 1) % 16 == 0 and j < 127:
                    f.write("\n    ")
            f.write("\n};\n\n")

        # -- Forward declarations for voice groups --
        f.write("// ── Forward declarations for voice groups ──\n")
        for vg_name in voice_groups:
            f.write(f"extern struct ToneData {vg_name}[];\n")
        f.write("\n")

        # -- Voice group definitions --
        f.write("// ── Voice group definitions ──\n")
        for vg_name, voices in voice_groups.items():
            f.write(f"struct ToneData {vg_name}[] = {{\n")
            for voice in voices:
                wav = _wav_expr(voice)
                kst = _keysplit_expr(voice)
                f.write(f"    {{ .type = 0x{voice['type']:02X}, .key = {voice['key']}, "
                        f".length = {voice['length']}, .pan_sweep = {voice['pan_sweep']}, "
                        f".wav = {wav}, "
                        f".attack = {voice['attack']}, .decay = {voice['decay']}, "
                        f".sustain = {voice['sustain']}, .release = {voice['release']}, "
                        f".keySplitTable = {kst} }},\n")
            f.write("};\n\n")

        # -- Song track data and headers --
        f.write("// ── Song track data and headers ──\n")

        # Collect all song names for the song table
        song_headers = {}  # song_name -> header variable name

        for song in songs_data:
            if song is None:
                continue
            sname = song['name']

            # Track data arrays
            for track_label, track_bytes in song['tracks']:
                f.write(f"static const u8 {track_label}[] = {{")
                for j, b in enumerate(track_bytes):
                    if j % 16 == 0:
                        f.write("\n    ")
                    f.write(f"0x{b:02X}")
                    if j < len(track_bytes) - 1:
                        f.write(", ")
                f.write("\n};\n\n")

            # Song header
            tc = song['header_track_count']
            bc = song['header_block_count']
            pri = song['header_pri']
            rev = song['header_rev']
            grp = song['header_grp']

            header_name = f"{sname}_header"
            song_headers[sname] = header_name

            if tc == 0:
                # No tracks (like mus_dummy)
                f.write(f"static struct {{ u8 tc; u8 bc; u8 pri; u8 rev; "
                        f"struct ToneData *tone; u8 *part[1]; }} {header_name} = {{\n")
                f.write(f"    {tc}, {bc}, {pri}, {rev},\n")
                f.write(f"    (struct ToneData *){grp},\n")
                f.write(f"    {{ NULL }}\n")
                f.write(f"}};\n\n")
            else:
                f.write(f"static struct {{ u8 tc; u8 bc; u8 pri; u8 rev; "
                        f"struct ToneData *tone; u8 *part[{tc}]; }} {header_name} = {{\n")
                f.write(f"    {tc}, {bc}, {pri}, {rev},\n")
                f.write(f"    (struct ToneData *){grp},\n")
                f.write(f"    {{")
                for j, (track_label, _) in enumerate(song['tracks']):
                    if j > 0:
                        f.write(",")
                    f.write(f" (u8 *){track_label}")
                f.write(f" }}\n")
                f.write(f"}};\n\n")

        # -- Dummy song header --
        f.write("// ── Dummy song header ──\n")
        f.write("static struct { u8 tc; u8 bc; u8 pri; u8 rev; "
                "struct ToneData *tone; u8 *part[1]; } dummy_song_header = {\n")
        f.write("    0, 0, 0, 0, NULL, { NULL }\n")
        f.write("};\n\n")

        # -- Song table --
        f.write("// ── Song table ──\n")
        f.write(f"const struct Song gSongTable[{len(song_table)}] = {{\n")
        for sname, ms, me in song_table:
            if sname == 'mus_dummy':
                f.write(f"    {{ (struct SongHeader *)&dummy_song_header, {ms}, {me} }},\n")
            elif sname in song_headers:
                f.write(f"    {{ (struct SongHeader *)&{song_headers[sname]}, {ms}, {me} }},\n")
            else:
                print(f"WARNING: Song '{sname}' in song_table but no parsed data",
                      file=sys.stderr)
                f.write(f"    {{ (struct SongHeader *)&dummy_song_header, {ms}, {me} }},  "
                        f"// WARNING: {sname} not found\n")
        f.write("};\n\n")

        # -- Cry tables --
        f.write("// ── Cry tables ──\n")
        f.write(f"struct ToneData gCryTable[{len(cries_forward)}] = {{\n")
        for cry_name in cries_forward:
            f.write(f"    {{ .type = 0x20, .key = 60, .length = 0, .pan_sweep = 0, "
                    f".wav = (struct WaveData *)raw_{cry_name}, "
                    f".attack = 0xFF, .decay = 0, .sustain = 0xFF, .release = 0, "
                    f".keySplitTable = NULL }},\n")
        f.write("};\n\n")

        f.write(f"struct ToneData gCryTable_Reverse[{len(cries_reverse)}] = {{\n")
        for cry_name in cries_reverse:
            f.write(f"    {{ .type = 0x30, .key = 60, .length = 0, .pan_sweep = 0, "
                    f".wav = (struct WaveData *)raw_{cry_name}, "
                    f".attack = 0xFF, .decay = 0, .sustain = 0xFF, .release = 0, "
                    f".keySplitTable = NULL }},\n")
        f.write("};\n\n")



# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Convert GBA assembly sound data to native C source files")
    parser.add_argument('--decomp-root', default='third_party/pokefirered/',
                        help='Path to the decomp root (default: third_party/pokefirered/)')
    parser.add_argument('--output-dir', default='src/',
                        help='Output directory (default: src/)')
    args = parser.parse_args()

    decomp_root = args.decomp_root
    if not decomp_root.endswith('/'):
        decomp_root += '/'
    output_dir = args.output_dir
    if not output_dir.endswith('/'):
        output_dir += '/'

    sound_dir = os.path.join(decomp_root, 'sound')

    # Parse MPlayDef.s
    print("Parsing MPlayDef.s...")
    mplaydef_path = os.path.join(sound_dir, 'MPlayDef.s')
    mplaydef = parse_mplaydef(mplaydef_path)
    print(f"  {len(mplaydef)} symbols defined")

    # Parse direct_sound_data.inc
    print("Parsing direct_sound_data.inc...")
    dsd_path = os.path.join(sound_dir, 'direct_sound_data.inc')
    direct_sounds = parse_direct_sound_data(dsd_path)
    print(f"  {len(direct_sounds)} direct sound samples")

    # Parse programmable_wave_data.inc
    print("Parsing programmable_wave_data.inc...")
    pwd_path = os.path.join(sound_dir, 'programmable_wave_data.inc')
    prog_waves = parse_programmable_wave_data(pwd_path)
    print(f"  {len(prog_waves)} programmable wave samples")

    # Parse keysplit_tables.inc
    print("Parsing keysplit_tables.inc...")
    kst_path = os.path.join(sound_dir, 'keysplit_tables.inc')
    keysplit_tables = parse_keysplit_tables(kst_path)
    print(f"  {len(keysplit_tables)} keysplit tables")

    # Parse voice_groups.inc
    print("Parsing voice_groups.inc...")
    vg_path = os.path.join(sound_dir, 'voice_groups.inc')
    voice_groups = parse_voice_groups(vg_path)
    total_voices = sum(len(v) for v in voice_groups.values())
    print(f"  {len(voice_groups)} voice groups, {total_voices} total voices")

    # Parse all song files
    print("Parsing song files...")
    songs_dir = os.path.join(sound_dir, 'songs')
    song_files = []
    # Collect all .s files from songs/ and songs/midi/
    for fn in sorted(os.listdir(songs_dir)):
        if fn.endswith('.s'):
            song_files.append(os.path.join(songs_dir, fn))
    midi_dir = os.path.join(songs_dir, 'midi')
    if os.path.isdir(midi_dir):
        for fn in sorted(os.listdir(midi_dir)):
            if fn.endswith('.s'):
                song_files.append(os.path.join(midi_dir, fn))

    songs_data = []
    song_name_map = {}
    for sf in song_files:
        song = parse_song_file(sf, mplaydef)
        if song is not None:
            songs_data.append(song)
            song_name_map[song['name']] = song
        else:
            print(f"  WARNING: Failed to parse {sf}", file=sys.stderr)

    print(f"  {len(songs_data)} songs parsed from {len(song_files)} files")

    # Parse song_table.inc
    print("Parsing song_table.inc...")
    st_path = os.path.join(sound_dir, 'song_table.inc')
    song_table = parse_song_table(st_path)
    print(f"  {len(song_table)} song table entries")

    # Parse cry_tables.inc
    print("Parsing cry_tables.inc...")
    ct_path = os.path.join(sound_dir, 'cry_tables.inc')
    cries_forward, cries_reverse = parse_cry_tables(ct_path)
    print(f"  {len(cries_forward)} forward cries, {len(cries_reverse)} reverse cries")

    # Order songs_data to match song_table order
    ordered_songs = []
    for sname, ms, me in song_table:
        if sname == 'mus_dummy':
            continue
        if sname in song_name_map:
            ordered_songs.append(song_name_map[sname])
        else:
            print(f"  WARNING: Song '{sname}' not found in parsed data", file=sys.stderr)

    # Generate output files
    os.makedirs(output_dir, exist_ok=True)

    print("Generating upstream_sound_samples.S...")
    asm_path = os.path.join(output_dir, 'upstream_sound_samples.S')
    generate_samples_asm(direct_sounds, prog_waves, decomp_root, asm_path)

    print("Generating upstream_sound_data.c...")
    c_path = os.path.join(output_dir, 'upstream_sound_data.c')
    generate_sound_data_c(direct_sounds, prog_waves, keysplit_tables,
                          voice_groups, songs_data, song_table,
                          cries_forward, cries_reverse, c_path)

    print("Done!")


if __name__ == '__main__':
    main()
