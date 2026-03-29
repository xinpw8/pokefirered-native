#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import gen_pfr_world_data  # noqa: E402


class PfrWorldDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.world = gen_pfr_world_data.build_world_data(ROOT)
        cls.maps_by_name = {item["name"]: item for item in cls.world["maps"]}
        cls.maps_by_id = {item["id"]: item for item in cls.world["maps"]}
        cls.map_groups = json.loads(
            (ROOT / "third_party/pokefirered/data/maps/map_groups.json").read_text()
        )

    def test_map_count_matches_map_groups(self):
        expected = sum(len(self.map_groups[group]) for group in self.map_groups["group_order"])
        self.assertEqual(expected, self.world["summary"]["map_count"])

    def test_tiles_match_layout_dimensions(self):
        for entry in self.world["maps"]:
            expected = entry["width"] * entry["height"]
            self.assertEqual(expected, len(entry["tiles"]["raw_blocks"]), entry["name"])
            self.assertEqual(expected, len(entry["tiles"]["behaviors"]), entry["name"])
            self.assertEqual(expected, len(entry["tiles"]["terrains"]), entry["name"])

    def test_connection_conflicts_are_limited_to_known_ambiguous_junctions(self):
        conflicts = self.world["connection_conflicts"]
        self.assertTrue(conflicts)
        self.assertTrue(all(item["source_map_name"] != "SaffronCity_Connection" for item in conflicts))
        source_names = {item["source_map_name"] for item in conflicts}
        dest_names = {item["dest_map_name"] for item in conflicts}
        expected = {"LavenderTown", "Route8", "VermilionCity", "Route11"}
        self.assertTrue(source_names.issubset(expected))
        self.assertTrue(dest_names.issubset(expected))

    def test_connected_overworld_alignment(self):
        pallet = self.maps_by_name["PalletTown"]
        route1 = self.maps_by_name["Route1"]
        self.assertEqual(pallet["atlas_x"], route1["atlas_x"])
        self.assertEqual(pallet["atlas_y"] - route1["height"], route1["atlas_y"])

    def test_static_warp_destinations_resolve(self):
        self.assertEqual([], self.world["static_warp_errors"])
        static_warps = [item for item in self.world["warp_report"] if not item["dynamic_dest"]]
        self.assertTrue(static_warps)
        self.assertTrue(all(item["valid_static_dest"] for item in static_warps))

    def test_directional_warps_have_required_direction(self):
        directional = [
            item
            for item in self.world["warp_report"]
            if item["warp_class"] in {
                "east_arrow",
                "west_arrow",
                "north_arrow",
                "south_arrow",
                "up_right_stair",
                "up_left_stair",
                "down_right_stair",
                "down_left_stair",
                "door",
            }
        ]
        self.assertTrue(directional)
        self.assertTrue(all(item["required_direction"] for item in directional))


if __name__ == "__main__":
    unittest.main()
