#!/usr/bin/env python3
"""Repair Bad Egg in pokefirered.sav by restoring from backup save slot."""
import struct, sys, shutil, os

SECTOR_SIZE = 4096
SECTOR_DATA_SIZE = 3968
SECTOR_COUNT = 32
SIGNATURE = 0x08012025
NUM_SECTORS_PER_SLOT = 14

# SaveBlock1 offsets
SB1_PARTY_COUNT = 0x0034
SB1_PARTY_BASE = 0x0038
POKEMON_SIZE = 100
BOXMON_SIZE = 80
SB1_ROUTE5_DAYCARE = 0x3C98
DAYCAREMON_SIZE = 140

# Sector footer offsets (within a 4096-byte sector)
FOOTER_ID = 4084
FOOTER_CHECKSUM = 4086
FOOTER_SIGNATURE = 4088
FOOTER_COUNTER = 4092

def read_sector_footer(data, sector_idx):
    base = sector_idx * SECTOR_SIZE
    sid = struct.unpack_from('<H', data, base + FOOTER_ID)[0]
    checksum = struct.unpack_from('<H', data, base + FOOTER_CHECKSUM)[0]
    signature = struct.unpack_from('<I', data, base + FOOTER_SIGNATURE)[0]
    counter = struct.unpack_from('<I', data, base + FOOTER_COUNTER)[0]
    return sid, checksum, signature, counter

def calc_sector_checksum(data, sector_idx, data_size=SECTOR_DATA_SIZE):
    base = sector_idx * SECTOR_SIZE
    checksum = 0
    for i in range(0, data_size, 4):
        checksum += struct.unpack_from('<I', data, base + i)[0]
        checksum &= 0xFFFFFFFF
    return ((checksum >> 16) + checksum) & 0xFFFF

def build_slot_map(data):
    """Build mapping of sector_id -> (flash_sector, counter) for both slots."""
    slots = {}  # sector_id -> list of (flash_sector, counter)
    for fs in range(SECTOR_COUNT):
        sid, checksum, signature, counter = read_sector_footer(data, fs)
        if signature != SIGNATURE:
            continue
        if sid > 13:  # Hall of Fame etc
            continue
        if sid not in slots:
            slots[sid] = []
        slots[sid].append((fs, counter))
    return slots

def get_active_backup_maps(data):
    """Return (active_map, backup_map) where each maps sector_id -> flash_sector."""
    slots = build_slot_map(data)
    active_map = {}
    backup_map = {}
    for sid, entries in slots.items():
        if len(entries) < 2:
            # Only one copy, treat as active
            active_map[sid] = entries[0][0]
            continue
        entries.sort(key=lambda x: x[1], reverse=True)
        active_map[sid] = entries[0][0]  # highest counter
        backup_map[sid] = entries[1][0]  # second highest
    return active_map, backup_map

def read_saveblock1_chunk(data, slot_map, sb1_offset, length):
    """Read bytes from SaveBlock1 at the given offset, spanning sectors as needed."""
    result = bytearray()
    remaining = length
    offset = sb1_offset
    while remaining > 0:
        # Which sector_id contains this offset?
        sector_id = 1 + (offset // SECTOR_DATA_SIZE)
        sector_offset = offset % SECTOR_DATA_SIZE
        chunk_len = min(remaining, SECTOR_DATA_SIZE - sector_offset)
        if sector_id not in slot_map:
            print(f"ERROR: sector_id {sector_id} not in slot map")
            return None
        flash_sector = slot_map[sector_id]
        file_offset = flash_sector * SECTOR_SIZE + sector_offset
        result.extend(data[file_offset:file_offset + chunk_len])
        remaining -= chunk_len
        offset += chunk_len
    return bytes(result)

def write_saveblock1_chunk(data, slot_map, sb1_offset, payload):
    """Write bytes to SaveBlock1 at the given offset, spanning sectors as needed."""
    remaining = len(payload)
    offset = sb1_offset
    payload_offset = 0
    modified_sectors = set()
    while remaining > 0:
        sector_id = 1 + (offset // SECTOR_DATA_SIZE)
        sector_offset = offset % SECTOR_DATA_SIZE
        chunk_len = min(remaining, SECTOR_DATA_SIZE - sector_offset)
        if sector_id not in slot_map:
            print(f"ERROR: sector_id {sector_id} not in slot map")
            return set()
        flash_sector = slot_map[sector_id]
        file_offset = flash_sector * SECTOR_SIZE + sector_offset
        data[file_offset:file_offset + chunk_len] = payload[payload_offset:payload_offset + chunk_len]
        modified_sectors.add((sector_id, flash_sector))
        remaining -= chunk_len
        offset += chunk_len
        payload_offset += chunk_len
    return modified_sectors

def update_sector_checksum(data, flash_sector, data_size=SECTOR_DATA_SIZE):
    """Recalculate and write sector checksum."""
    new_checksum = calc_sector_checksum(data, flash_sector, data_size)
    struct.pack_into('<H', data, flash_sector * SECTOR_SIZE + FOOTER_CHECKSUM, new_checksum)
    return new_checksum

def decode_boxmon(raw):
    """Decode a BoxPokemon from 80 bytes."""
    personality = struct.unpack_from('<I', raw, 0)[0]
    otId = struct.unpack_from('<I', raw, 4)[0]
    nickname_raw = raw[8:18]
    # Gen3 text decoding (simplified)
    GEN3_CHARS = {0xBB: 'A', 0xBC: 'B', 0xBD: 'C', 0xBE: 'D', 0xBF: 'E',
                  0xC0: 'F', 0xC1: 'G', 0xC2: 'H', 0xC3: 'I', 0xC4: 'J',
                  0xC5: 'K', 0xC6: 'L', 0xC7: 'M', 0xC8: 'N', 0xC9: 'O',
                  0xCA: 'P', 0xCB: 'Q', 0xCC: 'R', 0xCD: 'S', 0xCE: 'T',
                  0xCF: 'U', 0xD0: 'V', 0xD1: 'W', 0xD2: 'X', 0xD3: 'Y',
                  0xD4: 'Z', 0xD5: 'a', 0xD6: 'b', 0xD7: 'c', 0xD8: 'd',
                  0xD9: 'e', 0xDA: 'f', 0xDB: 'g', 0xDC: 'h', 0xDD: 'i',
                  0xDE: 'j', 0xDF: 'k', 0xE0: 'l', 0xE1: 'm', 0xE2: 'n',
                  0xE3: 'o', 0xE4: 'p', 0xE5: 'q', 0xE6: 'r', 0xE7: 's',
                  0xE8: 't', 0xE9: 'u', 0xEA: 'v', 0xEB: 'w', 0xEC: 'x',
                  0xED: 'y', 0xEE: 'z', 0xFF: ''}
    nickname = ''
    for b in nickname_raw:
        if b == 0xFF:
            break
        nickname += GEN3_CHARS.get(b, f'[{b:02X}]')
    
    flags = raw[0x13]
    isBadEgg = flags & 1
    hasSpecies = (flags >> 1) & 1
    isEgg = (flags >> 2) & 1
    checksum = struct.unpack_from('<H', raw, 0x1C)[0]
    
    # Decrypt secure data
    key = personality ^ otId
    secure = bytearray(raw[0x20:0x50])
    for i in range(0, 48, 4):
        word = struct.unpack_from('<I', secure, i)[0]
        word ^= key
        struct.pack_into('<I', secure, i, word)
    
    # Get species from substruct 0 (growth)
    order_table = [
        (0,1,2,3),(0,1,3,2),(0,2,1,3),(0,3,1,2),(0,2,3,1),(0,3,2,1),
        (1,0,2,3),(1,0,3,2),(2,0,1,3),(3,0,1,2),(2,0,3,1),(3,0,2,1),
        (1,2,0,3),(1,3,0,2),(2,1,0,3),(3,1,0,2),(2,3,0,1),(3,2,0,1),
        (1,2,3,0),(1,3,2,0),(2,1,3,0),(3,1,2,0),(2,3,1,0),(3,2,1,0)
    ]
    order = order_table[personality % 24]
    growth_slot = order[0]
    growth_data = secure[growth_slot * 12:(growth_slot + 1) * 12]
    species = struct.unpack_from('<H', growth_data, 0)[0]
    
    # Verify checksum
    calc_sum = 0
    for i in range(0, 48, 2):
        calc_sum += struct.unpack_from('<H', secure, i)[0]
    calc_sum &= 0xFFFF
    checksum_ok = (calc_sum == checksum)
    
    return {
        'personality': personality,
        'otId': otId,
        'nickname': nickname,
        'isBadEgg': isBadEgg,
        'hasSpecies': hasSpecies,
        'isEgg': isEgg,
        'checksum': checksum,
        'calc_checksum': calc_sum,
        'checksum_ok': checksum_ok,
        'species': species,
        'flags_byte': flags,
    }

def main():
    sav_path = '/home/spark-advantage/pokefirered-native/pokefirered.sav'
    
    with open(sav_path, 'rb') as f:
        data = bytearray(f.read())
    
    assert len(data) == 131072, f"Unexpected save file size: {len(data)}"
    
    active_map, backup_map = get_active_backup_maps(data)
    
    print("=== Active Slot Sector Map ===")
    for sid in sorted(active_map.keys()):
        fs = active_map[sid]
        _, _, _, counter = read_sector_footer(data, fs)
        print(f"  sector_id {sid:2d} -> flash {fs:2d} (offset 0x{fs*SECTOR_SIZE:05X}) counter={counter}")
    
    print("\n=== Backup Slot Sector Map ===")
    for sid in sorted(backup_map.keys()):
        fs = backup_map[sid]
        _, _, _, counter = read_sector_footer(data, fs)
        print(f"  sector_id {sid:2d} -> flash {fs:2d} (offset 0x{fs*SECTOR_SIZE:05X}) counter={counter}")
    
    # Read active party
    print("\n=== Active Party ===")
    party_count_raw = read_saveblock1_chunk(data, active_map, SB1_PARTY_COUNT, 1)
    party_count = party_count_raw[0]
    print(f"Party count: {party_count}")
    
    for i in range(min(party_count, 6)):
        offset = SB1_PARTY_BASE + i * POKEMON_SIZE
        pokemon_raw = read_saveblock1_chunk(data, active_map, offset, POKEMON_SIZE)
        info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
        status = "BAD EGG" if info['isBadEgg'] else ("EGG" if info['isEgg'] else "OK")
        print(f"  [{i}] species={info['species']:3d} nick={info['nickname']:10s} "
              f"pers=0x{info['personality']:08X} flags=0x{info['flags_byte']:02X} "
              f"chksum=0x{info['checksum']:04X} calc=0x{info['calc_checksum']:04X} "
              f"{'OK' if info['checksum_ok'] else 'MISMATCH'} {status}")
    
    # Read active daycare
    print("\n=== Active Route 5 Daycare ===")
    daycare_raw = read_saveblock1_chunk(data, active_map, SB1_ROUTE5_DAYCARE, DAYCAREMON_SIZE)
    dc_info = decode_boxmon(daycare_raw[:BOXMON_SIZE])
    dc_steps = struct.unpack_from('<I', daycare_raw, 136)[0]
    if dc_info['personality'] == 0 and dc_info['otId'] == 0:
        print("  EMPTY")
    else:
        status = "BAD EGG" if dc_info['isBadEgg'] else "OK"
        print(f"  species={dc_info['species']} nick={dc_info['nickname']} "
              f"pers=0x{dc_info['personality']:08X} steps={dc_steps} {status}")
    
    # Read backup party
    if backup_map:
        print("\n=== Backup Party ===")
        bp_count_raw = read_saveblock1_chunk(data, backup_map, SB1_PARTY_COUNT, 1)
        bp_count = bp_count_raw[0]
        print(f"Party count: {bp_count}")
        
        for i in range(min(bp_count, 6)):
            offset = SB1_PARTY_BASE + i * POKEMON_SIZE
            pokemon_raw = read_saveblock1_chunk(data, backup_map, offset, POKEMON_SIZE)
            info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
            status = "BAD EGG" if info['isBadEgg'] else ("EGG" if info['isEgg'] else "OK")
            print(f"  [{i}] species={info['species']:3d} nick={info['nickname']:10s} "
                  f"pers=0x{info['personality']:08X} flags=0x{info['flags_byte']:02X} "
                  f"chksum=0x{info['checksum']:04X} calc=0x{info['calc_checksum']:04X} "
                  f"{'OK' if info['checksum_ok'] else 'MISMATCH'} {status}")
        
        # Read backup daycare
        print("\n=== Backup Route 5 Daycare ===")
        bdc_raw = read_saveblock1_chunk(data, backup_map, SB1_ROUTE5_DAYCARE, DAYCAREMON_SIZE)
        bdc_info = decode_boxmon(bdc_raw[:BOXMON_SIZE])
        bdc_steps = struct.unpack_from('<I', bdc_raw, 136)[0]
        if bdc_info['personality'] == 0 and bdc_info['otId'] == 0:
            print("  EMPTY")
        else:
            status = "BAD EGG" if bdc_info['isBadEgg'] else "OK"
            print(f"  species={bdc_info['species']} nick={bdc_info['nickname']} "
                  f"pers=0x{bdc_info['personality']:08X} steps={bdc_steps} {status}")
    
    # --- REPAIR ---
    print("\n" + "="*60)
    print("=== REPAIR PLAN ===")
    
    # Find the Bad Egg in active party
    bad_egg_slot = None
    for i in range(min(party_count, 6)):
        offset = SB1_PARTY_BASE + i * POKEMON_SIZE
        pokemon_raw = read_saveblock1_chunk(data, active_map, offset, POKEMON_SIZE)
        info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
        if info['isBadEgg']:
            bad_egg_slot = i
            bad_egg_personality = info['personality']
            print(f"Found Bad Egg at active party slot {i} (personality 0x{info['personality']:08X}, nick={info['nickname']})")
            break
    
    if bad_egg_slot is None:
        print("No Bad Egg found in active party. Nothing to repair.")
        return
    
    # Find matching Pokemon in backup
    donor_slot = None
    donor_data = None
    if backup_map:
        bp_count_raw = read_saveblock1_chunk(data, backup_map, SB1_PARTY_COUNT, 1)
        bp_count = bp_count_raw[0]
        for i in range(min(bp_count, 6)):
            offset = SB1_PARTY_BASE + i * POKEMON_SIZE
            pokemon_raw = read_saveblock1_chunk(data, backup_map, offset, POKEMON_SIZE)
            info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
            if info['personality'] == bad_egg_personality and not info['isBadEgg']:
                donor_slot = i
                donor_data = pokemon_raw
                print(f"Found healthy donor in backup party slot {i}: species={info['species']} nick={info['nickname']}")
                break
    
    if donor_data is None:
        print("ERROR: No healthy donor found in backup for the Bad Egg personality. Cannot repair.")
        return
    
    # Also find Lapras in backup for daycare restoration
    lapras_data = None
    if backup_map:
        for i in range(min(bp_count, 6)):
            offset = SB1_PARTY_BASE + i * POKEMON_SIZE
            pokemon_raw = read_saveblock1_chunk(data, backup_map, offset, POKEMON_SIZE)
            info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
            if info['species'] == 131 and not info['isBadEgg']:  # Lapras
                lapras_data = pokemon_raw
                print(f"Found healthy Lapras in backup party slot {i}: nick={info['nickname']}")
                break
    
    # Check if active daycare is empty and Lapras is missing from active party
    active_has_lapras = False
    for i in range(min(party_count, 6)):
        offset = SB1_PARTY_BASE + i * POKEMON_SIZE
        pokemon_raw = read_saveblock1_chunk(data, active_map, offset, POKEMON_SIZE)
        info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
        if info['species'] == 131 and not info['isBadEgg']:
            active_has_lapras = True
            break
    
    daycare_empty = (dc_info['personality'] == 0 and dc_info['otId'] == 0)
    
    print(f"\nActive has Lapras in party: {active_has_lapras}")
    print(f"Active daycare empty: {daycare_empty}")
    
    # Apply repairs
    print("\n--- Applying repairs ---")
    modified_sectors = set()
    
    # 1. Replace Bad Egg with donor (Butterfree)
    print(f"1. Replacing Bad Egg at party[{bad_egg_slot}] with backup Butterfree...")
    target_offset = SB1_PARTY_BASE + bad_egg_slot * POKEMON_SIZE
    ms = write_saveblock1_chunk(data, active_map, target_offset, donor_data)
    modified_sectors.update(ms)
    
    # 2. Restore Lapras to daycare if missing and daycare is empty
    if lapras_data and daycare_empty and not active_has_lapras:
        print("2. Restoring Lapras to Route 5 daycare...")
        # Build DaycareMon: BoxPokemon(80) + DayCareMail(56) + steps(4) = 140
        daycare_mon = bytearray(DAYCAREMON_SIZE)
        daycare_mon[:BOXMON_SIZE] = lapras_data[:BOXMON_SIZE]  # BoxPokemon only
        struct.pack_into('<I', daycare_mon, 136, 0)  # steps = 0
        ms = write_saveblock1_chunk(data, active_map, SB1_ROUTE5_DAYCARE, bytes(daycare_mon))
        modified_sectors.update(ms)
    elif lapras_data and not active_has_lapras:
        print("2. SKIPPED: Daycare not empty or Lapras already in active party")
    else:
        print("2. SKIPPED: No Lapras donor found or already present")
    
    # 3. Recalculate checksums for modified sectors
    print("\n--- Recalculating sector checksums ---")
    # Need to know the data size for each sector_id
    # sector_id 0: SaveBlock2 (3968 or less)
    # sector_id 1-4: SaveBlock1 chunks (3968 each, last may be smaller)
    # sector_id 5-13: PokemonStorage chunks
    # For simplicity, use SECTOR_DATA_SIZE (3968) for all - the checksum function
    # just sums the data bytes, extra zeros don't matter
    
    for sid, fs in modified_sectors:
        old_checksum = struct.unpack_from('<H', data, fs * SECTOR_SIZE + FOOTER_CHECKSUM)[0]
        new_checksum = update_sector_checksum(data, fs)
        print(f"  sector_id {sid} (flash {fs}): checksum 0x{old_checksum:04X} -> 0x{new_checksum:04X}")
    
    # Write repaired save
    print("\n--- Writing repaired save ---")
    with open(sav_path, 'wb') as f:
        f.write(data)
    print(f"Wrote {len(data)} bytes to {sav_path}")
    
    # Verify repair
    print("\n=== VERIFICATION ===")
    with open(sav_path, 'rb') as f:
        vdata = bytearray(f.read())
    
    vactive, _ = get_active_backup_maps(vdata)
    
    vparty_count = read_saveblock1_chunk(vdata, vactive, SB1_PARTY_COUNT, 1)[0]
    print(f"Party count: {vparty_count}")
    for i in range(min(vparty_count, 6)):
        offset = SB1_PARTY_BASE + i * POKEMON_SIZE
        pokemon_raw = read_saveblock1_chunk(vdata, vactive, offset, POKEMON_SIZE)
        info = decode_boxmon(pokemon_raw[:BOXMON_SIZE])
        status = "BAD EGG" if info['isBadEgg'] else ("EGG" if info['isEgg'] else "OK")
        print(f"  [{i}] species={info['species']:3d} nick={info['nickname']:10s} "
              f"chksum {'OK' if info['checksum_ok'] else 'MISMATCH'} {status}")
    
    vdc_raw = read_saveblock1_chunk(vdata, vactive, SB1_ROUTE5_DAYCARE, DAYCAREMON_SIZE)
    vdc_info = decode_boxmon(vdc_raw[:BOXMON_SIZE])
    if vdc_info['personality'] == 0 and vdc_info['otId'] == 0:
        print("Route 5 daycare: EMPTY")
    else:
        status = "BAD EGG" if vdc_info['isBadEgg'] else "OK"
        print(f"Route 5 daycare: species={vdc_info['species']} nick={vdc_info['nickname']} {status}")
    
    print("\nRepair complete!")

if __name__ == '__main__':
    main()
