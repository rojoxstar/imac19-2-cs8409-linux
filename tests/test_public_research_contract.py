#!/usr/bin/env python3
"""Consistency tests for public clean-room research claims."""
from __future__ import annotations
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = json.loads((ROOT / "research/apple-dsp/models/current-status.json").read_text())

class PublicResearchContract(unittest.TestCase):
    def test_target_and_frozen_completeness(self):
        self.assertEqual(MODEL["target"]["macos_build"], "24G90")
        self.assertEqual(MODEL["target"]["architecture"], "x86_64")
        self.assertEqual((MODEL["completeness"]["child_src_percent"], MODEL["completeness"]["dualband_safety_percent"]), (88, 74))
    def test_unresolved_domain_is_not_overclaimed(self):
        self.assertFalse(MODEL["topology"]["dualband_is_physical_crossover"])
        self.assertEqual(MODEL["control_src"]["production_reachability"], "UNKNOWN")
        self.assertEqual(MODEL["control_src"]["coverage_condition"], "R == 0 or M > 0")
    def test_channel_permutation(self):
        self.assertEqual(MODEL["topology"]["internal_order"], ["TL", "TR", "WL", "WR"])
        self.assertEqual(MODEL["topology"]["output_order"], ["TL", "WL", "RT", "RW"])
        self.assertEqual(MODEL["topology"]["tas_addresses"]["0xda"], "left_woofer")
    def test_no_playback_clearance(self):
        self.assertFalse(MODEL["completeness"]["protection_safe_for_playback"])
        self.assertEqual(MODEL["completeness"]["whole_chain_headroom"], "PARTIAL")
        self.assertEqual(MODEL["blocker"]["corpus"], "RECOVERY-ONLY")

if __name__ == "__main__":
    unittest.main(verbosity=2)
