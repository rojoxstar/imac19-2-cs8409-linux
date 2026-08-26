#!/usr/bin/env python3
"""Offline contracts for the exact-iMac19,2 PLL1 S3 restore candidate."""

from __future__ import annotations

import hashlib
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "driver/source/cs8409.c"
PATCH_PATH = ROOT / "patches/local-imac19-2-pll1-restore-diagnostic.patch"
PATCH_SHA256 = "15ed025ebd6b7667e4e1c5e73b76d671eea1adae7f71c689827423b7b812cc2e"
PATCHED_SHA256 = "e81a9d1feb834bdff21277c14a9c7a645a722bed94c7ffd68e6243accfc921d8"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def apply_patch() -> tuple[str, str]:
    base = BASE_PATH.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="imac-pll1-test.") as tmp:
        tree = Path(tmp)
        target = tree / "driver/source/cs8409.c"
        target.parent.mkdir(parents=True)
        target.write_text(base, encoding="utf-8")
        subprocess.run(
            ["patch", "--batch", "--forward", "-p1", "-d", str(tree)],
            input=PATCH_PATH.read_bytes(),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        patched = target.read_text(encoding="utf-8")
    return base, patched


def function_text(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        raise AssertionError(f"function not found: {name}")
    depth = 1
    pos = match.end()
    while depth and pos < len(source):
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    if depth:
        raise AssertionError(f"unterminated function: {name}")
    return source[match.start() : pos]


class Pll1RestoreContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.base, cls.patched = apply_patch()
        cls.restore = function_text(cls.patched, "imac19_2_restore_pll1")
        cls.init = function_text(cls.patched, "imac_hw_init")

    def test_patch_and_result_are_pinned(self) -> None:
        self.assertEqual(sha256(PATCH_PATH.read_bytes()), PATCH_SHA256)
        self.assertEqual(sha256(self.patched.encode()), PATCHED_SHA256)

    def test_exact_model_gate_precedes_restore(self) -> None:
        gate = "codec->fixup_id == CS8409_FIXUP_IMAC19_2"
        self.assertIn(gate, self.init)
        gate_pos = self.init.index(gate)
        restore_pos = self.init.index("imac19_2_restore_pll1(codec)")
        tdm_pos = self.init.index("imac_cs8409_tdm_setup_amps12(codec)")
        i2c_pos = self.init.index("imac_enable_i2c(codec)")
        self.assertLess(gate_pos, restore_pos)
        self.assertLess(restore_pos, tdm_pos)
        self.assertLess(tdm_pos, i2c_pos)

    def test_restore_changes_only_pll1_enable_bit(self) -> None:
        self.assertEqual(self.restore.count("cs8409_vendor_coef_get("), 1)
        self.assertEqual(self.restore.count("cs8409_vendor_coef_set("), 1)
        self.assertIn("old_cfg | BIT(12)", self.restore)
        self.assertIn("if (changed)", self.restore)
        self.assertEqual(self.restore.count("CS8409_DEV_CFG1"), 2)
        self.assertNotIn("0xb008", self.restore.lower())
        self.assertNotIn("0x9008", self.restore.lower())
        self.assertNotIn("0x8008", self.restore.lower())

    def test_existing_mutex_serializes_coefficient_index(self) -> None:
        lock_pos = self.restore.index("guard(mutex)(&spec->i2c_mux)")
        get_pos = self.restore.index("cs8409_vendor_coef_get")
        set_pos = self.restore.index("cs8409_vendor_coef_set")
        log_pos = self.restore.index("codec_info")
        self.assertLess(lock_pos, get_pos)
        self.assertLess(get_pos, set_pos)
        self.assertLess(set_pos, log_pos)

    def test_no_unrelated_hardware_or_timing_delta(self) -> None:
        identical_tokens = (
            "snd_hda_codec_write(",
            "snd_hda_codec_read(",
            "cs8409_i2c_read(",
            "cs8409_i2c_write(",
            "cs8409_i2c_bulk_read(",
            "cs8409_i2c_bulk_write(",
            "imac_cs8409_coef_read(",
            "imac_cs8409_coef_write(",
            "usleep_range(",
            "msleep(",
            "fsleep(",
            "queue_delayed_work(",
            "cancel_delayed_work(",
            "cancel_delayed_work_sync(",
        )
        for token in identical_tokens:
            self.assertEqual(self.base.count(token), self.patched.count(token), token)
        self.assertEqual(
            self.patched.count("cs8409_vendor_coef_get("),
            self.base.count("cs8409_vendor_coef_get(") + 1,
        )
        self.assertEqual(
            self.patched.count("cs8409_vendor_coef_set("),
            self.base.count("cs8409_vendor_coef_set(") + 1,
        )

    def test_gain_rate_tas_and_pcm_contracts_are_unchanged(self) -> None:
        frozen_tokens = (
            "{ 0xd8, 0x02, 0xab }",
            "{ 0xda, 0x00, 0xab }",
            "{ 0xdc, 0x03, 0xab }",
            "{ 0xde, 0x01, 0xab }",
            "SNDRV_PCM_RATE_44100",
            "imac_tas576_boot_reset_setup(codec)",
            "imac_tas576_tdm_slot_setup(codec)",
            "imac_playback_pcm_prepare",
            "imac_playback_pcm_cleanup",
        )
        for token in frozen_tokens:
            self.assertEqual(self.base.count(token), self.patched.count(token), token)


if __name__ == "__main__":
    unittest.main(verbosity=2)
