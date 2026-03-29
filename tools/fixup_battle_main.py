#!/usr/bin/env python3
"""Fix battle_main.c static initializer for dynamic GBA addresses."""
import sys, re

path = sys.argv[1]
with open(path) as f:
    c = f.read()

# Remove the static const definition (3 lines)
c = re.sub(
    r'static const struct ScanlineEffectParams sIntroScanlineParams16Bit\s*=\s*\{[^}]+\};',
    '/* sIntroScanlineParams16Bit: moved to inline compound literal for dynamic addr compat */',
    c
)

# Replace usage with inline compound literal (runtime, not static)
c = c.replace(
    'ScanlineEffect_SetParams(sIntroScanlineParams16Bit)',
    'ScanlineEffect_SetParams((struct ScanlineEffectParams){ .dmaDest = &REG_BG3HOFS, .dmaControl = SCANLINE_EFFECT_DMACNT_16BIT, .initState = 1 })'
)

with open(path, 'w') as f:
    f.write(c)
print(f'fixup_battle_main: patched {path}')
