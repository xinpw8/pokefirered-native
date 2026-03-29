#!/usr/bin/env python3
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_SIGNATURE = 0x08012025

# The sector checksum is calculated as:
# sum all 32-bit words in the SECTOR_DATA_SIZE bytes of data
# then fold: (sum >> 16) + (sum & 0xFFFF)

def calc_sector_checksum(sector_data):
    """Calculate sector checksum matching upstream CalculateChecksum"""
    checksum = 0
    for i in range(0, len(sector_data) // 4 * 4, 4):
        val = struct.unpack_from("<I", sector_data, i)[0]
        checksum += val
        checksum &= 0xFFFFFFFF
    return ((checksum >> 16) + checksum) & 0xFFFF

# Let's read save.c's CalculateChecksum to be sure
# static u16 CalculateChecksum(void *data, u16 size)
# {
#     u16 i;
#     u32 checksum = 0;
#     for (i = 0; i < size / 4; i++)
#         checksum += ((u32 *)data)[i];
#     return (u16)((checksum >> 16) + checksum);
# }

# But NOTE: the checksum is calculated over just the USED bytes (locations[sectorId].size),
# not the full SECTOR_DATA_SIZE.
# From the sSaveSlotLayout:
#   sector 0: offset=0, size=min(sizeof(SaveBlock2), 3968)
#   sector 1: offset=0, size=min(sizeof(SaveBlock1), 3968)
#   sector 2: offset=3968, size=min(sizeof(SaveBlock1)-3968, 3968)
#   etc.
# SaveBlock1 is 0x3D68 = 15720 bytes
# Sector 1: 3968 bytes
# Sector 2: 3968 bytes
# Sector 3: 3968 bytes
# Sector 4: 15720 - 3*3968 = 15720 - 11904 = 3816 bytes

print("SaveBlock1 size: 0x3D68 = %d bytes" % 0x3D68)
print("Sector 1 data size: %d" % min(0x3D68, 3968))
print("Sector 2 data size: %d" % min(0x3D68 - 3968, 3968))
print("Sector 3 data size: %d" % min(0x3D68 - 2*3968, 3968))
print("Sector 4 data size: %d" % min(0x3D68 - 3*3968, 3968))

# Verify sector checksums
print("\n=== Verifying sector checksums ===")
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

# SaveBlock2 size is 0xF24 = 3876
sb2_size = 0xF24
sb1_size = 0x3D68

sector_data_sizes = {}
sector_data_sizes[0] = min(sb2_size, SECTOR_DATA_SIZE)

for chunk in range(4):
    sid = 1 + chunk
    remaining = sb1_size - chunk * SECTOR_DATA_SIZE
    sector_data_sizes[sid] = min(remaining, SECTOR_DATA_SIZE) if remaining > 0 else 0

# For PokemonStorage sectors, we need the struct size
# Not critical for our purpose, but let's note it
print("Sector data sizes for SaveBlock1:")
for sid in range(5):
    print("  sector_id %d: %d bytes" % (sid, sector_data_sizes[sid]))

# Verify active slot checksums
for sid in sorted(active_sectors.keys()):
    if sid > 4:
        continue
    fs = active_sectors[sid]
    sector_raw = data[fs*SECTOR_SIZE:fs*SECTOR_SIZE+SECTOR_DATA_SIZE]
    footer_start = fs * SECTOR_SIZE + 3968 + 116
    stored_checksum = struct.unpack_from("<H", data, footer_start + 2)[0]

    # Calculate over the actual data size
    if sid in sector_data_sizes:
        calc_cksum = calc_sector_checksum(sector_raw[:sector_data_sizes[sid]])
    else:
        calc_cksum = calc_sector_checksum(sector_raw)

    print("  sector_id %d (flash %d): stored=0x%04X, calc=0x%04X, match=%s" %
          (sid, fs, stored_checksum, calc_cksum, stored_checksum == calc_cksum))

# Now let's check: the route5DayCareMon is at SaveBlock1 offset 0x3C98
# This is in sector_id 4 (chunk 3) at sector offset: 0x3C98 - 3*3968 = 0x3C98 - 0x2E80 = 0x0E18
# The sector_id 4 data size is 3816 bytes
# 0x0E18 = 3608, which is WITHIN the 3816-byte valid region
print("\nroute5DayCareMon:")
print("  SaveBlock1 offset: 0x3C98")
print("  sector_id 4, chunk 3")
print("  offset within sector: 0x%04X = %d" % (0x3C98 - 3*3968, 0x3C98 - 3*3968))
print("  sector_id 4 data size: %d" % sector_data_sizes[4])
print("  route5DayCareMon is at bytes %d-%d within sector (size 140)" % (0x3C98 - 3*3968, 0x3C98 - 3*3968 + 140))
print("  fits within sector: %s" % ((0x3C98 - 3*3968 + 140) <= sector_data_sizes[4]))

# CRITICAL INFO: The Bad Egg (Party slot 4) has:
# - personality 0x711B9F48, otId 0x5C6C9C40 (SAME otId as Lapras)
# - All secure data is zeros -> decrypted = XOR key repeated
# - checksum 0x0000 (wrong - calc is 0x85F4)
# - isBadEgg=1, hasSpecies=1, isEgg=1
#
# The valid Lapras has:
# - personality 0x6D466F24, otId 0x5C6C9C40
# - Valid encrypted data, checksum matches
# - isBadEgg=0, hasSpecies=1, isEgg=0
#
# They share the same otId but different personality values.
# The Bad Egg's secure data is all zeros, meaning when decrypted = key repeated.
# This is classic corruption: the secure data got zeroed out.

print("\n" + "="*60)
print("SUMMARY OF FINDINGS")
print("="*60)
print("""
1. The Bad Egg is in PARTY SLOT 4 (index 4), NOT in the route5 daycare.
   - The route5DayCareMon slot is EMPTY (all zeros personality/otId/checksum)
   - The route5DayCareMon region contains game script text ("SORRY", etc.)

2. The Bad Egg at Party[4]:
   - personality: 0x711B9F48
   - otId:        0x5C6C9C40 (same as the valid Lapras)
   - nickname:    PP (not LAPPY)
   - ALL 48 bytes of secure/encrypted substruct data are ZERO
   - checksum: 0x0000 (should be 0x85F4 based on decrypted zeros)
   - flags: isBadEgg=1, hasSpecies=1, isEgg=1

3. A VALID Lapras exists in the BACKUP save slot's party:
   - Party slot 5 in backup (counter=163)
   - personality: 0x6D466F24
   - otId:        0x5C6C9C40
   - nickname:    LAPPY
   - species:     131 (Lapras)
   - All data valid, checksum matches
   - moves: Confuse Ray(109), Ice Beam(195), Thunderbolt(58), Psychic(240)
   - Friendship: 152, Met location: 134, Met level: 25

4. REPAIR STRATEGY:
   Option A: Copy the valid Lapras from backup Party[5] into current Party[4]
   Option B: Copy valid Lapras into route5DayCareMon and remove Bad Egg from party

   For either option, the sector checksum must be recalculated after modifying data.
""")

# Print exact file offsets needed for repair
print("=== EXACT OFFSETS FOR REPAIR ===")
print()

# Party slot 4 in active save
party_sb1_offset = 0x0038 + 4 * 100  # 0x01C0
print("Party slot 4 BoxPokemon in SaveBlock1: offset 0x%04X" % party_sb1_offset)
sid_for_party4 = party_sb1_offset // SECTOR_DATA_SIZE + 1
offset_in_sector_party4 = party_sb1_offset % SECTOR_DATA_SIZE
flash_sector_party4 = active_sectors[sid_for_party4]
file_offset_party4 = flash_sector_party4 * SECTOR_SIZE + offset_in_sector_party4
print("  sector_id: %d, offset in sector: 0x%04X" % (sid_for_party4, offset_in_sector_party4))
print("  flash sector: %d, FILE OFFSET: 0x%05X" % (flash_sector_party4, file_offset_party4))

# Party slot 5 in backup save (the valid Lapras)
backup_sectors = {}
for i in range(32):
    offset = i * SECTOR_SIZE
    footer_start = offset + 3968 + 116
    sector_id_val = struct.unpack_from("<H", data, footer_start)[0]
    signature = struct.unpack_from("<I", data, footer_start + 4)[0]
    counter = struct.unpack_from("<I", data, footer_start + 8)[0]
    if signature == SECTOR_SIGNATURE and sector_id_val < 14 and counter == 163:
        backup_sectors[sector_id_val] = i

party5_sb1_offset = 0x0038 + 5 * 100  # 0x0224
sid_for_party5 = party5_sb1_offset // SECTOR_DATA_SIZE + 1
offset_in_sector_party5 = party5_sb1_offset % SECTOR_DATA_SIZE
flash_sector_party5_bak = backup_sectors[sid_for_party5]
file_offset_party5_bak = flash_sector_party5_bak * SECTOR_SIZE + offset_in_sector_party5
print()
print("Valid Lapras (backup Party[5]) in SaveBlock1: offset 0x%04X" % party5_sb1_offset)
print("  sector_id: %d, offset in sector: 0x%04X" % (sid_for_party5, offset_in_sector_party5))
print("  flash sector: %d, FILE OFFSET: 0x%05X" % (flash_sector_party5_bak, file_offset_party5_bak))

# Party count offset
party_count_sb1_offset = 0x0034
sid_count = party_count_sb1_offset // SECTOR_DATA_SIZE + 1
offset_in_sector_count = party_count_sb1_offset % SECTOR_DATA_SIZE
flash_sector_count = active_sectors[sid_count]
file_offset_count = flash_sector_count * SECTOR_SIZE + offset_in_sector_count
print()
print("Party count in SaveBlock1: offset 0x%04X" % party_count_sb1_offset)
print("  sector_id: %d, offset in sector: 0x%04X" % (sid_count, offset_in_sector_count))
print("  flash sector: %d, FILE OFFSET: 0x%05X" % (flash_sector_count, file_offset_count))

# Lapras raw data from backup
lapras_boxmon = data[file_offset_party5_bak:file_offset_party5_bak+80]
# Pokemon extra data (status, level, hp, etc.) follows BoxPokemon
lapras_pokemon_extra = data[file_offset_party5_bak+80:file_offset_party5_bak+100]

print()
print("=== VALID LAPRAS RAW DATA (from backup Party[5]) ===")
print("BoxPokemon (80 bytes): %s" % lapras_boxmon.hex())
print("Pokemon extra (20 bytes): %s" % lapras_pokemon_extra.hex())
print("Full Pokemon struct (100 bytes): %s" % (lapras_boxmon + lapras_pokemon_extra).hex())

# What about sector checksum recalculation?
# After modifying sector data, we need to recalculate the sector checksum
print()
print("=== SECTOR CHECKSUM INFO ===")
for sid in [sid_for_party4, sid_count]:
    fs = active_sectors[sid]
    footer_offset = fs * SECTOR_SIZE + 3968 + 116
    print("sector_id %d: flash sector %d" % (sid, fs))
    print("  data start: file offset 0x%05X" % (fs * SECTOR_SIZE))
    print("  data size for checksum: %d bytes" % sector_data_sizes[sid])
    print("  checksum location: file offset 0x%05X" % (footer_offset + 2))
    print("  footer offset in file: 0x%05X" % footer_offset)
