#!/usr/bin/env python3
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_SIGNATURE = 0x08012025
LAPRAS = 131

gen3 = {}
for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    gen3[0xBB + i] = ch
for i, ch in enumerate("abcdefghijklmnopqrstuvwxyz"):
    gen3[0xD5 + i] = ch
gen3[0x00] = " "
gen3[0xFF] = ""
gen3[0xAD] = "-"
gen3[0xAE] = "."
gen3[0xB4] = "'"
gen3[0xAB] = "!"
gen3[0xAC] = "?"
gen3[0x01] = " "
for i, ch in enumerate("0123456789"):
    gen3[0xA1 + i] = ch

def decode_gen3(raw):
    result = ""
    for b in raw:
        if b == 0xFF:
            break
        result += gen3.get(b, "[0x%02X]" % b)
    return result

lapras_text = bytes([0xC6, 0xBB, 0xCA, 0xCC, 0xBB, 0xCD])
print("Looking for Gen3-encoded LAPRAS: %s" % lapras_text.hex())
for i in range(len(data) - 6):
    if data[i:i+6] == lapras_text:
        print("Found LAPRAS text at raw offset 0x%05X" % i)

active_sectors = {}
best_counter = {}
for i in range(32):
    offset = i * SECTOR_SIZE
    footer_start = offset + 3968 + 116
    sector_id_val = struct.unpack_from("<H", data, footer_start)[0]
    signature = struct.unpack_from("<I", data, footer_start + 4)[0]
    counter = struct.unpack_from("<I", data, footer_start + 8)[0]
    if signature == SECTOR_SIGNATURE and sector_id_val < 14:
        if sector_id_val not in best_counter or counter > best_counter[sector_id_val]:
            best_counter[sector_id_val] = counter
            active_sectors[sector_id_val] = i

saveblock1 = b""
for sid in range(1, 5):
    flash_sector = active_sectors[sid]
    offset = flash_sector * SECTOR_SIZE
    saveblock1 += data[offset:offset+SECTOR_DATA_SIZE]

print("\n=== Checking Player Party ===")
party_count = saveblock1[0x0034]
print("Party count: %d" % party_count)

POKEMON_SIZE = 100
party_offset = 0x0038

orders = [
    (0,1,2,3), (0,1,3,2), (0,2,1,3), (0,3,1,2), (0,2,3,1), (0,3,2,1),
    (1,0,2,3), (1,0,3,2), (2,0,1,3), (3,0,1,2), (2,0,3,1), (3,0,2,1),
    (1,2,0,3), (1,3,0,2), (2,1,0,3), (3,1,0,2), (2,3,0,1), (3,2,0,1),
    (1,2,3,0), (1,3,2,0), (2,1,3,0), (3,1,2,0), (2,3,1,0), (3,2,1,0),
]

for i in range(min(party_count, 6)):
    off = party_offset + i * POKEMON_SIZE
    boxmon = saveblock1[off:off+80]
    personality = struct.unpack_from("<I", boxmon, 0)[0]
    otId = struct.unpack_from("<I", boxmon, 4)[0]
    nickname = boxmon[8:18]
    flags_byte = boxmon[19]
    checksum = struct.unpack_from("<H", boxmon, 28)[0]
    pokemon_extra = saveblock1[off+80:off+100]
    level = pokemon_extra[4]
    hp = struct.unpack_from("<H", pokemon_extra, 6)[0]
    maxHP = struct.unpack_from("<H", pokemon_extra, 8)[0]

    key = personality ^ otId
    sec = boxmon[32:80]
    dec = b""
    for j in range(0, 48, 4):
        val = struct.unpack_from("<I", sec, j)[0]
        dec += struct.pack("<I", val ^ key)

    order = orders[personality % 24]
    growth = dec[order[0]*12:(order[0]+1)*12]
    species = struct.unpack_from("<H", growth, 0)[0]

    cksum_calc = 0
    for j in range(0, 48, 2):
        cksum_calc += struct.unpack_from("<H", dec, j)[0]
    cksum_calc &= 0xFFFF

    isBadEgg = flags_byte & 1
    hasSpecies = (flags_byte >> 1) & 1

    print("  Party[%d]: species=%d, nick=[%s], lv=%d, hp=%d/%d, pers=0x%08X, badEgg=%d, hasSp=%d, cksum=0x%04X(calc=0x%04X %s)" %
          (i, species, decode_gen3(nickname), level, hp, maxHP, personality, isBadEgg, hasSpecies, checksum, cksum_calc, "OK" if checksum == cksum_calc else "BAD"))

# Decode text in route5DayCareMon region
print("\n=== route5DayCareMon region decoded as Gen3 text ===")
route5_offset = 0x3C98
region = saveblock1[route5_offset:route5_offset+160]
for i in range(0, min(80, len(region))):
    b = region[i]
    ch = gen3.get(b, "?0x%02X" % b)
    if b == 0xFF:
        ch = "<END>"
    elif b == 0x00:
        ch = "<NUL>"
    print("  +0x%02X: 0x%02X = %s" % (i, b, ch))

# Search entire save for Lapras
print("\n=== Searching entire save for BoxPokemon with species=131 (Lapras) ===")
found_count = 0
for raw_off in range(0, len(data) - 80, 4):
    personality = struct.unpack_from("<I", data, raw_off)[0]
    otId = struct.unpack_from("<I", data, raw_off + 4)[0]
    checksum = struct.unpack_from("<H", data, raw_off + 28)[0]

    if personality == 0 or personality == 0xFFFFFFFF:
        continue
    if checksum == 0 or checksum == 0xFFFF:
        continue

    key = personality ^ otId
    sec = data[raw_off+32:raw_off+80]
    if len(sec) < 48:
        continue

    dec = b""
    for j in range(0, 48, 4):
        val = struct.unpack_from("<I", sec, j)[0]
        dec += struct.pack("<I", val ^ key)

    order = orders[personality % 24]
    growth = dec[order[0]*12:(order[0]+1)*12]
    species = struct.unpack_from("<H", growth, 0)[0]

    if species == LAPRAS:
        cksum_calc = 0
        for j in range(0, 48, 2):
            cksum_calc += struct.unpack_from("<H", dec, j)[0]
        cksum_calc &= 0xFFFF

        nickname = data[raw_off+8:raw_off+18]
        flags_byte = data[raw_off+19]
        isBadEgg = flags_byte & 1

        print("  FOUND Lapras at raw offset 0x%05X: pers=0x%08X, otId=0x%08X, nick=[%s], badEgg=%d, cksum=0x%04X(calc=0x%04X %s)" %
              (raw_off, personality, otId, decode_gen3(nickname), isBadEgg, checksum, cksum_calc, "OK" if checksum == cksum_calc else "BAD"))
        found_count += 1

if found_count == 0:
    print("  No Lapras found in save file!")

# Also check the backup save file
print("\n=== Also checking backup save ===")
import os
bak = sys.argv[1] + ".bak_before_recovery"
if os.path.exists(bak):
    with open(bak, "rb") as f:
        bdata = f.read()
    print("Backup save size: %d" % len(bdata))

    bak_found = 0
    for raw_off in range(0, len(bdata) - 80, 4):
        personality = struct.unpack_from("<I", bdata, raw_off)[0]
        otId = struct.unpack_from("<I", bdata, raw_off + 4)[0]
        checksum = struct.unpack_from("<H", bdata, raw_off + 28)[0]

        if personality == 0 or personality == 0xFFFFFFFF:
            continue
        if checksum == 0 or checksum == 0xFFFF:
            continue

        key = personality ^ otId
        sec = bdata[raw_off+32:raw_off+80]
        if len(sec) < 48:
            continue

        dec = b""
        for j in range(0, 48, 4):
            val = struct.unpack_from("<I", sec, j)[0]
            dec += struct.pack("<I", val ^ key)

        order = orders[personality % 24]
        growth = dec[order[0]*12:(order[0]+1)*12]
        species = struct.unpack_from("<H", growth, 0)[0]

        if species == LAPRAS:
            cksum_calc = 0
            for j in range(0, 48, 2):
                cksum_calc += struct.unpack_from("<H", dec, j)[0]
            cksum_calc &= 0xFFFF

            nickname = bdata[raw_off+8:raw_off+18]
            flags_byte = bdata[raw_off+19]
            isBadEgg = flags_byte & 1

            print("  FOUND Lapras at raw offset 0x%05X: pers=0x%08X, otId=0x%08X, nick=[%s], badEgg=%d, cksum=0x%04X(calc=0x%04X %s)" %
                  (raw_off, personality, otId, decode_gen3(nickname), isBadEgg, checksum, cksum_calc, "OK" if checksum == cksum_calc else "BAD"))
            bak_found += 1
    if bak_found == 0:
        print("  No Lapras found in backup!")
else:
    print("  No backup file found")
