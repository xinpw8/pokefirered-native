#!/usr/bin/env python3
"""Repair Bad Egg: replace it with Lapras from backup slot."""
import struct

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SIGNATURE = 0x08012025
SB1_PARTY_COUNT = 0x0034
SB1_PARTY_BASE = 0x0038
POKEMON_SIZE = 100
BOXMON_SIZE = 80
FOOTER_ID = 4084
FOOTER_CHECKSUM = 4086
FOOTER_SIGNATURE = 4088
FOOTER_COUNTER = 4092

def read_sector_footer(data, fs):
    base = fs * SECTOR_SIZE
    return (struct.unpack_from('<H', data, base + FOOTER_ID)[0],
            struct.unpack_from('<H', data, base + FOOTER_CHECKSUM)[0],
            struct.unpack_from('<I', data, base + FOOTER_SIGNATURE)[0],
            struct.unpack_from('<I', data, base + FOOTER_COUNTER)[0])

def get_slot_maps(data):
    slots = {}
    for fs in range(32):
        sid, ck, sig, cnt = read_sector_footer(data, fs)
        if sig != SIGNATURE or sid > 13:
            continue
        slots.setdefault(sid, []).append((fs, cnt))
    active, backup = {}, {}
    for sid, entries in slots.items():
        entries.sort(key=lambda x: x[1], reverse=True)
        active[sid] = entries[0][0]
        if len(entries) > 1:
            backup[sid] = entries[1][0]
    return active, backup

def sb1_read(data, slot_map, offset, length):
    result = bytearray()
    while length > 0:
        sid = 1 + (offset // SECTOR_DATA_SIZE)
        so = offset % SECTOR_DATA_SIZE
        n = min(length, SECTOR_DATA_SIZE - so)
        fo = slot_map[sid] * SECTOR_SIZE + so
        result.extend(data[fo:fo + n])
        length -= n
        offset += n
    return bytes(result)

def sb1_write(data, slot_map, offset, payload):
    modified = set()
    pi = 0
    remaining = len(payload)
    while remaining > 0:
        sid = 1 + (offset // SECTOR_DATA_SIZE)
        so = offset % SECTOR_DATA_SIZE
        n = min(remaining, SECTOR_DATA_SIZE - so)
        fo = slot_map[sid] * SECTOR_SIZE + so
        data[fo:fo + n] = payload[pi:pi + n]
        modified.add((sid, slot_map[sid]))
        remaining -= n
        offset += n
        pi += n
    return modified

def fix_checksum(data, fs):
    base = fs * SECTOR_SIZE
    s = 0
    for i in range(0, SECTOR_DATA_SIZE, 4):
        s += struct.unpack_from('<I', data, base + i)[0]
        s &= 0xFFFFFFFF
    c = ((s >> 16) + s) & 0xFFFF
    struct.pack_into('<H', data, base + FOOTER_CHECKSUM, c)
    return c

def main():
    path = '/home/spark-advantage/pokefirered-native/pokefirered.sav'
    with open(path, 'rb') as f:
        data = bytearray(f.read())

    active, backup = get_slot_maps(data)

    # Get Lapras from backup Party[5]
    lapras_raw = sb1_read(data, backup, SB1_PARTY_BASE + 5 * POKEMON_SIZE, POKEMON_SIZE)
    pers = struct.unpack_from('<I', lapras_raw, 0)[0]
    flags = lapras_raw[0x13]
    print(f"Backup Party[5]: personality=0x{pers:08X} flags=0x{flags:02X}")
    assert flags & 1 == 0, "Lapras is also a Bad Egg!"
    
    # Decrypt to verify species
    key = struct.unpack_from('<I', lapras_raw, 0)[0] ^ struct.unpack_from('<I', lapras_raw, 4)[0]
    secure = bytearray(lapras_raw[0x20:0x50])
    for i in range(0, 48, 4):
        w = struct.unpack_from('<I', secure, i)[0] ^ key
        struct.pack_into('<I', secure, i, w)
    order_table = [
        (0,1,2,3),(0,1,3,2),(0,2,1,3),(0,3,1,2),(0,2,3,1),(0,3,2,1),
        (1,0,2,3),(1,0,3,2),(2,0,1,3),(3,0,1,2),(2,0,3,1),(3,0,2,1),
        (1,2,0,3),(1,3,0,2),(2,1,0,3),(3,1,0,2),(2,3,0,1),(3,2,0,1),
        (1,2,3,0),(1,3,2,0),(2,1,3,0),(3,1,2,0),(2,3,1,0),(3,2,1,0)
    ]
    order = order_table[pers % 24]
    g = secure[order[0]*12:(order[0]+1)*12]
    species = struct.unpack_from('<H', g, 0)[0]
    print(f"Lapras species={species} (expected 131)")
    assert species == 131, f"Not Lapras! species={species}"

    # Write Lapras over Bad Egg at active Party[4]
    target = SB1_PARTY_BASE + 4 * POKEMON_SIZE
    modified = sb1_write(data, active, target, lapras_raw)
    print(f"Wrote Lapras to active Party[4]")

    # Fix checksums
    for sid, fs in modified:
        old = struct.unpack_from('<H', data, fs * SECTOR_SIZE + FOOTER_CHECKSUM)[0]
        new = fix_checksum(data, fs)
        print(f"  sector_id {sid} flash {fs}: checksum 0x{old:04X} -> 0x{new:04X}")

    with open(path, 'wb') as f:
        f.write(data)
    print(f"\nSave repaired! Bad Egg replaced with Lapras (LAPPY).")

    # Verify
    with open(path, 'rb') as f:
        vdata = bytearray(f.read())
    va, _ = get_slot_maps(vdata)
    pc = sb1_read(vdata, va, SB1_PARTY_COUNT, 1)[0]
    print(f"\nVerification - Party count: {pc}")
    for i in range(min(pc, 6)):
        raw = sb1_read(vdata, va, SB1_PARTY_BASE + i * POKEMON_SIZE, POKEMON_SIZE)
        p = struct.unpack_from('<I', raw, 0)[0]
        fl = raw[0x13]
        bad = "BAD EGG" if fl & 1 else "OK"
        key2 = p ^ struct.unpack_from('<I', raw, 4)[0]
        sec = bytearray(raw[0x20:0x50])
        for j in range(0, 48, 4):
            w = struct.unpack_from('<I', sec, j)[0] ^ key2
            struct.pack_into('<I', sec, j, w)
        o = order_table[p % 24]
        gr = sec[o[0]*12:(o[0]+1)*12]
        sp = struct.unpack_from('<H', gr, 0)[0]
        ck_stored = struct.unpack_from('<H', raw, 0x1C)[0]
        ck_calc = sum(struct.unpack_from('<H', sec, k)[0] for k in range(0, 48, 2)) & 0xFFFF
        ck_ok = "OK" if ck_stored == ck_calc else "MISMATCH"
        print(f"  [{i}] species={sp:3d} flags=0x{fl:02X} checksum {ck_ok} {bad}")

if __name__ == '__main__':
    main()
