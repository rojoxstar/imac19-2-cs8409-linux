#!/usr/bin/env python3
"""Offline equivalence checks for the D12 existing-read diagnostic patch."""

from __future__ import annotations

import hashlib
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASE_PATH = ROOT / "driver/source/cs8409.c"
PATCH_PATH = ROOT / "diagnostics/d12-coef0-rmw-logging.patch"
PATCH_SHA256 = "a6bd43649482e51e212467e8ff1e322cef9f7ebe2fd703291ba528e3a9b26b2c"
PATCHED_SHA256 = "7047e1c104ac10a43b05d33b27fd8b7ff1bf7c50701a77f4a04be0044890612e"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def function_span(source: str, name: str) -> tuple[int, int]:
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
    return match.start(), pos


def apply_patch() -> tuple[str, str]:
    base = BASE_PATH.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="imac-d12-test.") as tmp:
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


class D12DiagnosticContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.base, cls.patched = apply_patch()
        cls.base_span = function_span(cls.base, "cs8409_disable_i2c_clock")
        cls.patched_span = function_span(cls.patched, "cs8409_disable_i2c_clock")
        cls.base_fn = cls.base[slice(*cls.base_span)]
        cls.patched_fn = cls.patched[slice(*cls.patched_span)]

    def test_patch_and_result_are_exactly_pinned(self) -> None:
        self.assertEqual(sha256(PATCH_PATH.read_bytes()), PATCH_SHA256)
        self.assertEqual(sha256(self.patched.encode()), PATCHED_SHA256)

    def test_only_disable_function_changes(self) -> None:
        base_without = self.base[: self.base_span[0]] + self.base[self.base_span[1] :]
        patched_without = (
            self.patched[: self.patched_span[0]] + self.patched[self.patched_span[1] :]
        )
        self.assertEqual(base_without, patched_without)

    def test_hardware_facing_call_inventory_is_identical(self) -> None:
        tokens = (
            "snd_hda_codec_write(",
            "snd_hda_codec_read(",
            "cs8409_vendor_coef_get(",
            "cs8409_vendor_coef_set(",
            "cs8409_i2c_read(",
            "cs8409_i2c_write(",
            "cs8409_i2c_bulk_read(",
            "cs8409_i2c_bulk_write(",
            "imac_cs8409_coef_read(",
            "imac_cs8409_coef_write(",
            "usleep_range(",
            "msleep(",
            "fsleep(",
        )
        for token in tokens:
            self.assertEqual(self.base.count(token), self.patched.count(token), token)

    def test_disable_path_preserves_one_get_one_set_and_mask(self) -> None:
        self.assertEqual(self.base_fn.count("cs8409_vendor_coef_get("), 1)
        self.assertEqual(self.patched_fn.count("cs8409_vendor_coef_get("), 1)
        self.assertEqual(self.base_fn.count("cs8409_vendor_coef_set("), 1)
        self.assertEqual(self.patched_fn.count("cs8409_vendor_coef_set("), 1)
        self.assertEqual(self.base_fn.count("0xfffffff7"), 1)
        self.assertEqual(self.patched_fn.count("0xfffffff7"), 1)

        get_pos = self.patched_fn.index("coef_old = cs8409_vendor_coef_get")
        mask_pos = self.patched_fn.index("coef_new = coef_old & 0xfffffff7")
        set_pos = self.patched_fn.index("cs8409_vendor_coef_set(spec->codec, 0x0, coef_new)")
        flag_pos = self.patched_fn.index("spec->i2c_clck_enabled = 0", set_pos)
        log_pos = self.patched_fn.index("codec_info(codec", flag_pos)
        self.assertLess(get_pos, mask_pos)
        self.assertLess(mask_pos, set_pos)
        self.assertLess(set_pos, flag_pos)
        self.assertLess(flag_pos, log_pos)

    def test_logging_is_exact_model_only_and_post_rmw(self) -> None:
        self.assertIn("codec->fixup_id == CS8409_FIXUP_IMAC19_2", self.patched_fn)
        self.assertIn("rmw_done &&", self.patched_fn)
        self.assertIn("iMac D12 diag: disable-path coef0 old=", self.patched_fn)
        self.assertNotIn("codec_dbg", self.patched_fn)

    def test_no_new_pm_state_or_clock_scheduling(self) -> None:
        for token in (
            "queue_delayed_work(",
            "cancel_delayed_work(",
            "cancel_delayed_work_sync(",
            ".suspend =",
            ".resume =",
            ".stream_pm =",
        ):
            self.assertEqual(self.base.count(token), self.patched.count(token), token)


if __name__ == "__main__":
    unittest.main(verbosity=2)
