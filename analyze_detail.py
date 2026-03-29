#!/usr/bin/env python3
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_SIGNATURE = 0x08012025

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

orders = [
    (0,1,2,3), (0,1,3,2), (0,2,1,3), (0,3,1,2), (0,2,3,1), (0,3,2,1),
    (1,0,2,3), (1,0,3,2), (2,0,1,3), (3,0,1,2), (2,0,3,1), (3,0,2,1),
    (1,2,0,3), (1,3,0,2), (2,1,0,3), (3,1,0,2), (2,3,0,1), (3,2,0,1),
    (1,2,3,0), (1,3,2,0), (2,1,3,0), (3,1,2,0), (2,3,1,0), (3,2,1,0),
]

def full_parse_boxmon(raw80, label):
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

    print("\n=== %s ===" % label)
    print("Raw 80 bytes hex: %s" % raw80.hex())
    print("personality:  0x%08X" % personality)
    print("otId:         0x%08X" % otId)
    print("nickname:     [%s] (hex: %s)" % (decode_gen3(nickname), nickname.hex()))
    print("language:     %d" % language)
    print("flags byte:   0x%02X (isBadEgg=%d, hasSpecies=%d, isEgg=%d)" % (flags_byte, isBadEgg, hasSpecies, isEgg))
    print("otName:       [%s] (hex: %s)" % (decode_gen3(otName), otName.hex()))
    print("markings:     %d" % markings)
    print("checksum:     0x%04X" % checksum)
    print("unknown:      0x%04X" % unknown)

    key = personality ^ otId
    print("XOR key:      0x%08X" % key)

    dec = b""
    for j in range(0, 48, 4):
        val = struct.unpack_from("<I", secure_raw, j)[0]
        dec += struct.pack("<I", val ^ key)

    order = orders[personality % 24]
    print("personality %% 24 = %d, order = %s" % (personality % 24, order))

    cksum_calc = 0
    for j in range(0, 48, 2):
        cksum_calc += struct.unpack_from("<H", dec, j)[0]
    cksum_calc &= 0xFFFF
    print("Calculated checksum: 0x%04X (stored: 0x%04X, match: %s)" % (cksum_calc, checksum, checksum == cksum_calc))

    stype_names = ["Growth", "Attacks", "EVs/Condition", "Misc"]
    for stype in range(4):
        phys_slot = order[stype]
        sub = dec[phys_slot*12:(phys_slot+1)*12]
        print("\n  Substruct %d (%s) at physical slot %d:" % (stype, stype_names[stype], phys_slot))
        print("    hex: %s" % sub.hex())

        if stype == 0:
            species = struct.unpack_from("<H", sub, 0)[0]
            heldItem = struct.unpack_from("<H", sub, 2)[0]
            experience = struct.unpack_from("<I", sub, 4)[0]
            ppBonuses = sub[8]
            friendship = sub[9]
            print("    species=%d, heldItem=%d, exp=%d, ppBonuses=%d, friendship=%d" % (species, heldItem, experience, ppBonuses, friendship))
        elif stype == 1:
            moves = struct.unpack_from("<4H", sub, 0)
            pp = list(sub[8:12])
            print("    moves=%s, pp=%s" % (list(moves), pp))
        elif stype == 2:
            print("    EVs: HP=%d Atk=%d Def=%d Spd=%d SpA=%d SpD=%d" % (sub[0], sub[1], sub[2], sub[3], sub[4], sub[5]))
        elif stype == 3:
            pokerus = sub[0]
            metLocation = sub[1]
            originInfo = struct.unpack_from("<H", sub, 2)[0]
            ivsEggAbility = struct.unpack_from("<I", sub, 4)[0]
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
            print("    pokerus=%d metLoc=%d metLv=%d metGame=%d pokeball=%d otGender=%d" % (pokerus, metLocation, metLevel, metGame, pokeball, otGender))
            print("    IVs: HP=%d Atk=%d Def=%d Spd=%d SpA=%d SpD=%d" % (hpIV, atkIV, defIV, spdIV, spaIV, spdIV2))
            print("    isEgg(sub3)=%d abilityNum=%d" % (isEggBit, abilityNum))

    return personality, otId, checksum, cksum_calc, dec, order

# Active sectors
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

# ============================================================
# 1. Party slot 4 (the Bad Egg)
# ============================================================
print("="*60)
print("PARTY SLOT 4 (Bad Egg)")
print("="*60)
party_offset = 0x0038
POKEMON_SIZE = 100
off = party_offset + 4 * POKEMON_SIZE
boxmon = saveblock1[off:off+80]
pokemon_extra = saveblock1[off+80:off+100]
level = pokemon_extra[4]
hp = struct.unpack_from("<H", pokemon_extra, 6)[0]
maxHP = struct.unpack_from("<H", pokemon_extra, 8)[0]
print("Level: %d, HP: %d/%d" % (level, hp, maxHP))
print("Status: 0x%08X" % struct.unpack_from("<I", pokemon_extra, 0)[0])
full_parse_boxmon(boxmon, "Party slot 4")

# ============================================================
# 2. The Lapras found at raw offset 0x1822C
# ============================================================
print("\n" + "="*60)
print("LAPRAS at raw offset 0x1822C")
print("="*60)
lapras_raw = data[0x1822C:0x1822C+80]
full_parse_boxmon(lapras_raw, "Lapras at 0x1822C")

# Figure out which sector/block this belongs to
raw_off = 0x1822C
flash_sector_num = raw_off // SECTOR_SIZE
offset_in_sector = raw_off % SECTOR_SIZE
print("\nRaw offset 0x%05X is in flash sector %d, offset 0x%04X within sector" % (raw_off, flash_sector_num, offset_in_sector))

# What sector_id is in flash sector 24?
footer_start = flash_sector_num * SECTOR_SIZE + 3968 + 116
sector_id_here = struct.unpack_from("<H", data, footer_start)[0]
sig_here = struct.unpack_from("<I", data, footer_start + 4)[0]
counter_here = struct.unpack_from("<I", data, footer_start + 8)[0]
print("Flash sector %d has sector_id=%d, signature=0x%08X, counter=%d" % (flash_sector_num, sector_id_here, sig_here, counter_here))

# Is this in the backup save slot?
if counter_here < 164:
    print("This is in the BACKUP save slot (counter %d < active 164)" % counter_here)
else:
    print("This is in the ACTIVE save slot")

# Where in SaveBlock1 would this be?
# sector_id 1 -> SaveBlock1 offset 0*3968 = 0
# sector_id 2 -> SaveBlock1 offset 1*3968 = 3968
# etc.
if 1 <= sector_id_here <= 4:
    sb1_offset = (sector_id_here - 1) * SECTOR_DATA_SIZE + offset_in_sector
    print("This maps to SaveBlock1 offset 0x%04X" % sb1_offset)

# ============================================================
# 3. Check the route5DayCareMon region more carefully
# ============================================================
print("\n" + "="*60)
print("ROUTE5 DAYCARE REGION ANALYSIS")
print("="*60)

# The route5DayCareMon has zero personality/otId/checksum, but non-zero "secure" data
# The text in the region reads: "SORRY", "YAY??", "THANK YOU", "BYE.BYE"
# These look like game script text, not Pokemon data
# This suggests the route5DayCareMon slot does NOT contain a valid Pokemon

# Let's check if the Lapras was maybe in the party before and got corrupted
# Or if there's data elsewhere

# Check the backup slot's route5DayCareMon
print("\n=== Checking backup slot route5DayCareMon ===")
backup_sectors = {}
for i in range(32):
    offset = i * SECTOR_SIZE
    footer_start = offset + 3968 + 116
    sector_id_val = struct.unpack_from("<H", data, footer_start)[0]
    signature = struct.unpack_from("<I", data, footer_start + 4)[0]
    counter = struct.unpack_from("<I", data, footer_start + 8)[0]
    if signature == SECTOR_SIGNATURE and sector_id_val < 14 and counter == 163:
        backup_sectors[sector_id_val] = i

print("Backup sector mapping:")
for sid in sorted(backup_sectors.keys()):
    fs = backup_sectors[sid]
    print("  sector_id %d -> flash sector %d" % (sid, fs))

if 1 in backup_sectors and 2 in backup_sectors and 3 in backup_sectors and 4 in backup_sectors:
    backup_sb1 = b""
    for sid in range(1, 5):
        flash_sector = backup_sectors[sid]
        offset = flash_sector * SECTOR_SIZE
        backup_sb1 += data[offset:offset+SECTOR_DATA_SIZE]

    route5_off = 0x3C98
    boxmon_bak = backup_sb1[route5_off:route5_off+80]
    personality_bak = struct.unpack_from("<I", boxmon_bak, 0)[0]
    otId_bak = struct.unpack_from("<I", boxmon_bak, 4)[0]
    checksum_bak = struct.unpack_from("<H", boxmon_bak, 28)[0]
    print("\nBackup route5DayCareMon: personality=0x%08X, otId=0x%08X, checksum=0x%04X" % (personality_bak, otId_bak, checksum_bak))
    if personality_bak != 0:
        full_parse_boxmon(boxmon_bak, "Backup route5DayCareMon")

    # Also check backup party
    party_count_bak = backup_sb1[0x0034]
    print("\nBackup party count: %d" % party_count_bak)
    for i in range(min(party_count_bak, 6)):
        off = party_offset + i * POKEMON_SIZE
        boxmon = backup_sb1[off:off+80]
        personality = struct.unpack_from("<I", boxmon, 0)[0]
        otId = struct.unpack_from("<I", boxmon, 4)[0]
        nickname = boxmon[8:18]
        flags_byte = boxmon[19]
        checksum = struct.unpack_from("<H", boxmon, 28)[0]
        key = personality ^ otId
        sec = boxmon[32:80]
        dec = b""
        for j in range(0, 48, 4):
            val = struct.unpack_from("<I", sec, j)[0]
            dec += struct.pack("<I", val ^ key)
        order = orders[personality % 24]
        growth = dec[order[0]*12:(order[0]+1)*12]
        species = struct.unpack_from("<H", growth, 0)[0]
        isBadEgg = flags_byte & 1
        cksum_calc = 0
        for j in range(0, 48, 2):
            cksum_calc += struct.unpack_from("<H", dec, j)[0]
        cksum_calc &= 0xFFFF
        print("  Backup Party[%d]: species=%d, nick=[%s], badEgg=%d, cksum=0x%04X(calc=0x%04X %s)" %
              (i, species, decode_gen3(nickname), isBadEgg, checksum, cksum_calc, "OK" if checksum == cksum_calc else "BAD"))
