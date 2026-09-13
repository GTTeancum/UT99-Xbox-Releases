"""Regression for removing a reach spec without corrupting navigation arrays."""
import struct
import unittest

import convert_ps2_seekfree_maps as ps2
import convert_seekfree_console_maps as package
from package_properties import read_tags


class ReachSpecRemapTest(unittest.TestCase):
    def test_removed_middle_edge_remaps_all_arrays_and_keeps_sentinel(self):
        ci = package.write_compact_index
        # Empty FURL, model, three reaches, ApproxTime, FirstDeleted,
        # sixteen TextBlocks and empty TravelInfo, matching ULevel::Serialize.
        tail = b"\0" * 5 + struct.pack("<ii", 7777, 1) + ci(1) + ci(3)
        for start, end in ((1, 2), (2, 999), (2, 3)):
            tail += struct.pack("<i", 100) + ci(start) + ci(end) + struct.pack("<iiiB", 40, 40, 1, 0)
        tail += struct.pack("<i", 0) + b"\0" * 18
        remap = {}
        _, old_count, new_count = ps2.sanitize_level_tail(tail, {1, 2, 3}, remap)
        self.assertEqual((old_count, new_count), (3, 2))
        self.assertEqual(remap, {0: 0, 2: 1})

        names = ["None", "Paths", "upstreamPaths", "PrunedPaths"]
        body = ci(-1) + ci(-1) + b"\0" * 12 + ci(-1)
        for name in names[1:]:
            for i, value in enumerate((2, 1, 0, -1)):
                body += ps2.encode_tag(names, {"name": name, "type": "IntProperty"}, struct.pack("<i", value), i)
        body += ci(0)
        result = ps2.remap_navigation_paths(body, names, {"class_index": -1}, remap)
        pos = ps2.stack_body_payload_offset(result, {"class_index": -1}, 0)
        tags = list(read_tags(result, names, pos, len(result)))
        for name in names[1:]:
            values = [struct.unpack("<i", tag["value"])[0] for tag in tags if tag["name"] == name]
            self.assertEqual(values, [1, 0] + [-1] * 14)


if __name__ == "__main__":
    unittest.main()
