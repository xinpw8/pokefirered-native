#!/usr/bin/env python3
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_SIGNATURE = 0x08012025

# Active sector mapping (highest counter)
best_counter = {}
active_sectors = {}
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

print("Active save slot sector mapping (sector_id -> flash_sector):")
for sid in sorted(active_sectors.keys()):
    fs = active_sectors[sid]
    print("  sector_id %2d -> flash sector %2d (file offset 0x%05X, counter=%d)" % (sid, fs, fs * SECTOR_SIZE, best_counter[sid]))

# Reconstruct full SaveBlock1 from sector_ids 1-4
saveblock1 = b""
for sid in range(1, 5):
    flash_sector = active_sectors[sid]
    offset = flash_sector * SECTOR_SIZE
    saveblock1 += data[offset:offset+SECTOR_DATA_SIZE]

print("\nReconstructed SaveBlock1: %d bytes" % len(saveblock1))

# Gen3 text decode
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

def parse_boxmon(raw80, label=""):
    personality = struct.unpack_from("<I", raw80, 0)[0]
    otId = struct.unpack_from("<I", raw80, 4)[0]
    nickname = raw80[8:18]
    language = raw80[18]
    flags_byte = raw80[19]
    otName = raw80[20:27]
    markings = raw80[27]
    checksum = struct.unpack_from("<H", raw80, 28)[0]
    unknown = struct.unpack_from("<H", raw80, 30)[0]
    secure_raw = raw80[32:80]

    isBadEgg = flags_byte & 1
    hasSpecies = (flags_byte >> 1) & 1
    isEgg = (flags_byte >> 2) & 1
    blockBoxRS = (flags_byte >> 3) & 1

    print("\n=== %s ===" % label)
    print("personality:  0x%08X (%d)" % (personality, personality))
    print("otId:         0x%08X (%d)" % (otId, otId))
    print("nickname hex: %s" % nickname.hex())
    print("nickname:     [%s]" % decode_gen3(nickname))
    print("language:     %d" % language)
    print("flags byte:   0x%02X (isBadEgg=%d, hasSpecies=%d, isEgg=%d, blockBoxRS=%d)" % (flags_byte, isBadEgg, hasSpecies, isEgg, blockBoxRS))
    print("otName hex:   %s" % otName.hex())
    print("otName:       [%s]" % decode_gen3(otName))
    print("markings:     %d" % markings)
    print("checksum:     0x%04X" % checksum)
    print("unknown:      0x%04X" % unknown)
    print("secure hex:   %s" % secure_raw.hex())

    if personality == 0 and otId == 0 and checksum == 0:
        print("  -> ALL ZEROS (empty slot or no valid Pokemon)")
        return

    # Decrypt
    key = personality ^ otId
    print("XOR key (personality ^ otId): 0x%08X" % key)

    dec_u32 = []
    for j in range(0, 48, 4):
        val = struct.unpack_from("<I", secure_raw, j)[0]
        dec_u32.append(val ^ key)

    decrypted = b""
    for val in dec_u32:
        decrypted += struct.pack("<I", val)
    print("decrypted secure: %s" % decrypted.hex())

    # Substruct order
    orders = [
        (0,1,2,3), (0,1,3,2), (0,2,1,3), (0,3,1,2), (0,2,3,1), (0,3,2,1),
        (1,0,2,3), (1,0,3,2), (2,0,1,3), (3,0,1,2), (2,0,3,1), (3,0,2,1),
        (1,2,0,3), (1,3,0,2), (2,1,0,3), (3,1,0,2), (2,3,0,1), (3,2,0,1),
        (1,2,3,0), (1,3,2,0), (2,1,3,0), (3,1,2,0), (2,3,1,0), (3,2,1,0),
    ]
    order = orders[personality % 24]
    print("personality %% 24 = %d, substruct order: %s" % (personality % 24, order))

    stype_names = ["Growth", "Attacks", "EVs/Condition", "Misc"]
    for stype in range(4):
        phys_slot = order[stype]
        sub = decrypted[phys_slot*12:(phys_slot+1)*12]
        print("\n  Substruct %d (%s) at physical slot %d:" % (stype, stype_names[stype], phys_slot))
        print("    raw: %s" % sub.hex())

        if stype == 0:  # Growth
            species = struct.unpack_from("<H", sub, 0)[0]
            heldItem = struct.unpack_from("<H", sub, 2)[0]
            experience = struct.unpack_from("<I", sub, 4)[0]
            ppBonuses = sub[8]
            friendship = sub[9]
            print("    species=%d, heldItem=%d, exp=%d, ppBonuses=%d, friendship=%d" % (species, heldItem, experience, ppBonuses, friendship))
        elif stype == 1:  # Attacks
            moves = struct.unpack_from("<4H", sub, 0)
            pp = list(sub[8:12])
            print("    moves=%s, pp=%s" % (moves, pp))
        elif stype == 2:  # EVs
            print("    EVs: HP=%d, Atk=%d, Def=%d, Spd=%d, SpA=%d, SpD=%d" % (sub[0], sub[1], sub[2], sub[3], sub[4], sub[5]))
            print("    Contest: cool=%d, beauty=%d, cute=%d, smart=%d, tough=%d, sheen=%d" % (sub[6], sub[7], sub[8], sub[9], sub[10], sub[11]))
        elif stype == 3:  # Misc
            pokerus = sub[0]
            metLocation = sub[1]
            originInfo = struct.unpack_from("<H", sub, 2)[0]
            ivsEggAbility = struct.unpack_from("<I", sub, 4)[0]
            ribbons = struct.unpack_from("<I", sub, 8)[0]
            hpIV = ivsEggAbility & 0x1F
            atkIV = (ivsEggAbility >> 5) & 0x1F
            defIV = (ivsEggAbility >> 10) & 0x1F
            spdIV = (ivsEggAbility >> 15) & 0x1F
            spaIV = (ivsEggAbility >> 20) & 0x1F
            spdIV2 = (ivsEggAbility >> 25) & 0x1F
            isEggBit = (ivsEggAbility >> 30) & 1
            abilityNum = (ivsEggAbility >> 31) & 1
            metLevel = originInfo & 0x7F
            metGame = (originInfo >> 7) & 0xF
            pokeball = (originInfo >> 11) & 0xF
            otGender = (originInfo >> 15) & 1
            print("    pokerus=%d, metLocation=%d" % (pokerus, metLocation))
            print("    metLevel=%d, metGame=%d, pokeball=%d, otGender=%d" % (metLevel, metGame, pokeball, otGender))
            print("    IVs: HP=%d, Atk=%d, Def=%d, Spd=%d, SpA=%d, SpD=%d" % (hpIV, atkIV, defIV, spdIV, spaIV, spdIV2))
            print("    isEgg(sub3)=%d, abilityNum=%d" % (isEggBit, abilityNum))
            print("    ribbons=0x%08X" % ribbons)

    # Verify checksum
    checksum_calc = 0
    for j in range(0, 48, 2):
        checksum_calc += struct.unpack_from("<H", decrypted, j)[0]
    checksum_calc &= 0xFFFF
    print("\n  Stored checksum:     0x%04X" % checksum)
    print("  Calculated checksum: 0x%04X" % checksum_calc)
    print("  Match: %s" % (checksum == checksum_calc))

# ============================================================
# MAIN DAYCARE (offset 0x2F80)
# ============================================================
daycare_offset = 0x2F80
print("\n" + "="*60)
print("MAIN DAYCARE at SaveBlock1 offset 0x%04X" % daycare_offset)
print("="*60)

for mon_idx in range(2):
    mon_offset = daycare_offset + mon_idx * 140
    boxmon_raw = saveblock1[mon_offset:mon_offset+80]
    steps_offset = mon_offset + 80 + 54 + 2  # after mail + padding
    if steps_offset + 4 <= len(saveblock1):
        steps = struct.unpack_from("<I", saveblock1, steps_offset)[0]
    else:
        steps = -1
    parse_boxmon(boxmon_raw, "Main DayCare mon %d (steps=%d)" % (mon_idx, steps))

# ============================================================
# ROUTE 5 DAYCARE MON (offset 0x3C98)
# ============================================================
route5_offset = 0x3C98
print("\n" + "="*60)
print("ROUTE 5 DAYCARE MON at SaveBlock1 offset 0x%04X" % route5_offset)
print("="*60)

boxmon_raw = saveblock1[route5_offset:route5_offset+80]
steps_offset = route5_offset + 80 + 54 + 2
if steps_offset + 4 <= len(saveblock1):
    steps = struct.unpack_from("<I", saveblock1, steps_offset)[0]
else:
    steps = -1
parse_boxmon(boxmon_raw, "route5DayCareMon (steps=%d)" % steps)

# Also dump raw hex around route5DayCareMon for context
print("\n=== Raw hex dump of route5DayCareMon region ===")
region_start = route5_offset
region_end = min(route5_offset + 160, len(saveblock1))
for i in range(region_start, region_end, 16):
    hexstr = " ".join("%02X" % saveblock1[j] for j in range(i, min(i+16, region_end)))
    ascstr = "".join(chr(saveblock1[j]) if 32 <= saveblock1[j] < 127 else "." for j in range(i, min(i+16, region_end)))
    print("  0x%04X: %-48s %s" % (i, hexstr, ascstr))

# Check if padding assumption is wrong - maybe no padding
print("\n=== Checking DaycareMon size by trying different sizes ===")
# If no padding: DaycareMon = 80 + 54 + 4 = 138
# If 2-byte pad: DaycareMon = 80 + 54 + 2 + 4 = 140
# Let's check both by seeing if the main daycare offspringPersonality makes sense
for daycaremon_size in [136, 138, 140, 142, 144]:
    dc_end = daycare_offset + 2 * daycaremon_size
    if dc_end + 3 < len(saveblock1):
        ofsp = struct.unpack_from("<H", saveblock1, dc_end)[0]
        stepc = saveblock1[dc_end + 2]
        print("  DaycareMon size=%d: offspringPersonality=0x%04X, stepCounter=%d" % (daycaremon_size, ofsp, stepc))
