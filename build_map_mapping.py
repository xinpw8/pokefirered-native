"""
Build pfr_to_red_map.json: Maps FireRed map IDs to Red map IDs.

Strategy:
1. For overworld maps (towns, cities, routes): direct name matching
2. For interior/dungeon maps: use region_map_section to find parent overworld,
   then map parent overworld to Red map ID
3. For Sevii Islands / maps with no Red equivalent: map to -1
"""

import json
import re

with open("/home/spark-advantage/pokefirered-native/red_map_data.json") as f:
    red_data = json.load(f)

with open("/home/spark-advantage/pokefirered-native/pfr_map_data.json") as f:
    pfr_data = json.load(f)

red_maps = [r for r in red_data["regions"] if r["name"] != "Kanto"]
pfr_maps = [r for r in pfr_data["regions"] if r["name"] != "Kanto"]

# Build MAPSEC -> Red ID mapping
mapsec_to_red = {
    "MAPSEC_PALLET_TOWN": "0",
    "MAPSEC_VIRIDIAN_CITY": "1",
    "MAPSEC_PEWTER_CITY": "2",
    "MAPSEC_CERULEAN_CITY": "3",
    "MAPSEC_LAVENDER_TOWN": "4",
    "MAPSEC_VERMILION_CITY": "5",
    "MAPSEC_CELADON_CITY": "6",
    "MAPSEC_FUCHSIA_CITY": "7",
    "MAPSEC_CINNABAR_ISLAND": "8",
    "MAPSEC_INDIGO_PLATEAU": "9",
    "MAPSEC_SAFFRON_CITY": "10",
    "MAPSEC_ROUTE_1": "12",
    "MAPSEC_ROUTE_2": "13",
    "MAPSEC_ROUTE_3": "14",
    "MAPSEC_ROUTE_4": "15",
    "MAPSEC_ROUTE_5": "16",
    "MAPSEC_ROUTE_6": "17",
    "MAPSEC_ROUTE_7": "18",
    "MAPSEC_ROUTE_8": "19",
    "MAPSEC_ROUTE_9": "20",
    "MAPSEC_ROUTE_10": "21",
    "MAPSEC_ROUTE_11": "22",
    "MAPSEC_ROUTE_12": "23",
    "MAPSEC_ROUTE_13": "24",
    "MAPSEC_ROUTE_14": "25",
    "MAPSEC_ROUTE_15": "26",
    "MAPSEC_ROUTE_16": "27",
    "MAPSEC_ROUTE_17": "28",
    "MAPSEC_ROUTE_18": "29",
    "MAPSEC_ROUTE_19": "30",
    "MAPSEC_ROUTE_20": "31",
    "MAPSEC_ROUTE_21": "32",
    "MAPSEC_ROUTE_22": "33",
    "MAPSEC_ROUTE_23": "34",
    "MAPSEC_ROUTE_24": "35",
    "MAPSEC_ROUTE_25": "36",
    # Dungeons and special areas
    "MAPSEC_VIRIDIAN_FOREST": "51",
    "MAPSEC_MT_MOON": "59",
    "MAPSEC_S_S_ANNE": "94",
    "MAPSEC_UNDERGROUND_PATH": "119",
    "MAPSEC_UNDERGROUND_PATH_2": "121",
    "MAPSEC_DIGLETTS_CAVE": "197",
    "MAPSEC_KANTO_VICTORY_ROAD": "194",
    "MAPSEC_ROCKET_HIDEOUT": "199",
    "MAPSEC_SILPH_CO": "181",
    "MAPSEC_POKEMON_MANSION": "165",
    "MAPSEC_KANTO_SAFARI_ZONE": "220",
    "MAPSEC_CERULEAN_CAVE": "228",
    "MAPSEC_POKEMON_LEAGUE": "174",
    "MAPSEC_ROCK_TUNNEL": "82",
    "MAPSEC_SEAFOAM_ISLANDS": "192",
    "MAPSEC_POKEMON_TOWER": "142",
    "MAPSEC_POWER_PLANT": "83",
}

# Direct name mappings for overworld maps (FireRed name -> Red ID)
pfr_overworld_to_red = {
    "PalletTown": "0",
    "ViridianCity": "1",
    "PewterCity": "2",
    "CeruleanCity": "3",
    "LavenderTown": "4",
    "VermilionCity": "5",
    "CeladonCity": "6",
    "FuchsiaCity": "7",
    "CinnabarIsland": "8",
    "IndigoPlateau_Exterior": "9",
    "SaffronCity": "10",
    "SaffronCity_Connection": "10",
    "Route1": "12",
    "Route2": "13",
    "Route3": "14",
    "Route4": "15",
    "Route5": "16",
    "Route6": "17",
    "Route7": "18",
    "Route8": "19",
    "Route9": "20",
    "Route10": "21",
    "Route11": "22",
    "Route12": "23",
    "Route13": "24",
    "Route14": "25",
    "Route15": "26",
    "Route16": "27",
    "Route17": "28",
    "Route18": "29",
    "Route19": "30",
    "Route20": "31",
    "Route21_North": "32",
    "Route21_South": "32",
    "Route22": "33",
    "Route23": "34",
    "Route24": "35",
    "Route25": "36",
    "ViridianForest": "51",
    "PowerPlant": "83",
}

mapping = {}
unmapped = []

for m in pfr_maps:
    pfr_id = str(m["id"])
    name = m["name"]
    mapsec = m.get("region_map_section", "")

    # Try direct overworld match first
    if name in pfr_overworld_to_red:
        mapping[pfr_id] = pfr_overworld_to_red[name]
        continue

    # Try mapsec mapping (for interiors and dungeons)
    if mapsec and mapsec in mapsec_to_red:
        mapping[pfr_id] = mapsec_to_red[mapsec]
        continue

    # For maps without mapsec, try extracting parent from name
    parts = name.split("_")
    base = parts[0]
    if base in pfr_overworld_to_red:
        mapping[pfr_id] = pfr_overworld_to_red[base]
        continue

    # Try CamelCase city names: "CeladonCity" from parts[0]+"City" etc.
    if len(parts) >= 2:
        combined = parts[0] + parts[1]
        for city_name in pfr_overworld_to_red:
            if city_name.lower().replace("_", "") == combined.lower():
                mapping[pfr_id] = pfr_overworld_to_red[city_name]
                break
        if pfr_id in mapping:
            continue

    # Sevii Islands and other FR-only content: map to -1
    unmapped.append((pfr_id, name, mapsec))
    mapping[pfr_id] = "-1"

# Sort by integer key for readability
sorted_mapping = {k: mapping[k] for k in sorted(mapping.keys(), key=lambda x: int(x))}

with open("/home/spark-advantage/pokefirered-native/pfr_to_red_map.json", "w") as f:
    json.dump(sorted_mapping, f, indent=2)

total = len(pfr_maps)
mapped_count = sum(1 for v in mapping.values() if v != "-1")
print("Total PFR maps:", total)
print("Mapped to Red:", mapped_count)
print("Unmapped (Sevii/new):", total - mapped_count)
print()
if unmapped:
    print("Unmapped maps:")
    for uid, uname, umapsec in unmapped:
        print("  %s: %s (%s)" % (uid, uname, umapsec))
