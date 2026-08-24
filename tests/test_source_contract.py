#!/usr/bin/env python3
"""Offline V10 public-export source contracts; never accesses hardware."""

from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "driver/source/cs8409.c"
HEADER_PATH = ROOT / "driver/source/cs8409.h"
TABLES_PATH = ROOT / "driver/source/cs8409-tables.c"
SOURCE = SOURCE_PATH.read_text(encoding="utf-8")
HEADER = HEADER_PATH.read_text(encoding="utf-8")
TABLES = TABLES_PATH.read_text(encoding="utf-8")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function_body(name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", SOURCE, re.S)
    if not match:
        raise AssertionError(f"function not found: {name}")
    start = match.end()
    depth = 1
    pos = start
    while depth and pos < len(SOURCE):
        if SOURCE[pos] == "{":
            depth += 1
        elif SOURCE[pos] == "}":
            depth -= 1
        pos += 1
    if depth:
        raise AssertionError(f"unterminated function: {name}")
    return SOURCE[start : pos - 1]


class Identity(unittest.TestCase):
    def test_exact_v10_source_hashes(self) -> None:
        expected = {
            SOURCE_PATH: "b111af117b6cbe87a7a6328971a3b733e8e13dbd2a481bf5ea034503b0d5517a",
            HEADER_PATH: "6ce59ec45fb6c35735914a545ec703cbc65464c8163f023bcdd8965ec83a1b65",
            TABLES_PATH: "9b935dc45703e23164c5b64750d750dad1197e4987ae3bb2be0e29d921354965",
        }
        for path, expected_digest in expected.items():
            self.assertEqual(digest(path), expected_digest, path.name)

    def test_spdx_headers_are_preserved(self) -> None:
        self.assertTrue(SOURCE.startswith("// SPDX-License-Identifier: GPL-2.0-or-later"))
        self.assertTrue(HEADER.startswith("/* SPDX-License-Identifier: GPL-2.0-or-later */"))
        self.assertTrue(TABLES.startswith("// SPDX-License-Identifier: GPL-2.0-only"))


class V10Contracts(unittest.TestCase):
    def test_exact_machine_quirk_exists(self) -> None:
        self.assertIn(
            'SND_PCI_QUIRK(0x106b, 0x0f00, "Apple iMac19,2 CS8409",',
            TABLES,
        )

    def test_rate_is_44100_only(self) -> None:
        block = re.search(
            r"cs42l83_apple_pcm_analog_playback\s*=\s*\{(?P<body>.*?)\};",
            SOURCE,
            re.S,
        ).group("body")
        self.assertIn("SNDRV_PCM_RATE_44100", block)
        self.assertNotIn("SNDRV_PCM_RATE_48000", block)
        self.assertIn("SNDRV_PCM_FMTBIT_S32_LE", block)
        self.assertIn(".maxbps = 32", block)

    def test_open_does_not_enable_cs42_buffers(self) -> None:
        body = function_body("imac_playback_pcm_open")
        self.assertNotIn("imac_cs42l83_buffers_on", body)
        self.assertIn("snd_hda_multi_out_analog_open", body)
        self.assertIn("active_streams |=", body)

    def test_prepare_and_cleanup_own_buffer_state(self) -> None:
        prepare = function_body("imac_playback_pcm_prepare")
        cleanup = function_body("imac_playback_pcm_cleanup")
        close = function_body("imac_playback_pcm_close")
        self.assertIn("imac_cs42l83_buffers_on", prepare)
        self.assertIn("imac_cs42l83_buffers_off", cleanup)
        self.assertNotIn("imac_cs42l83_buffers_", close)

    def test_init_result_publication_is_final(self) -> None:
        body = function_body("cs8409_init")
        fixup = body.index("snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT)")
        publish = body.rindex("spec->imac_init_error = ret")
        self.assertLess(fixup, publish)
        self.assertIn("codec->fixup_id == CS8409_FIXUP_IMAC19_2", body)

    def test_pcm_gates_precede_model_work(self) -> None:
        opened = function_body("imac_playback_pcm_open")
        prepared = function_body("imac_playback_pcm_prepare")
        open_gate = opened.index("CS8409_FIXUP_IMAC19_2 && ret < 0")
        prepare_gate = prepared.index("CS8409_FIXUP_IMAC19_2 && ret < 0")
        self.assertLess(open_gate, opened.index("snd_hda_multi_out_analog_open"))
        self.assertLess(prepare_gate, prepared.index("snd_hda_multi_out_analog_prepare"))
        self.assertLess(prepare_gate, prepared.index("imac_cs8409_tdm_setup_amps12"))

    def test_selector_and_gain_values_are_frozen(self) -> None:
        block = re.search(
            r"imac19_2_tas5764\s*=\s*\{(?P<body>.*?)\n\};", SOURCE, re.S
        ).group("body")
        for value in (
            "{ 0xd8, 0x02, 0xab }",
            "{ 0xda, 0x00, 0xab }",
            "{ 0xdc, 0x03, 0xab }",
            "{ 0xde, 0x01, 0xab }",
            ".analog_control = 0x55",
        ):
            self.assertIn(value, block)

    def test_side_device_and_stream_id_workarounds_remain(self) -> None:
        side = function_body("imac_tdm_extra_sync_setup")
        cleanup = function_body("imac_playback_pcm_cleanup")
        for value in (
            "dev.addr = 0x64",
            "0x0014, 0x00e4",
            "dev.addr = 0x28",
            "0x0005, 0x0000",
            "0x0004, 0x0051",
        ):
            self.assertIn(value, side)
        self.assertIn("0x02/0x03 stream clear skipped", cleanup)

    def test_production_has_no_v11_logging(self) -> None:
        self.assertNotIn("iMac V11 diag:", SOURCE)

    def test_no_speculative_devcfg1_restore(self) -> None:
        imac_tdm = function_body("imac_cs8409_tdm_setup_amps12")
        self.assertNotIn("imac_cs8409_coef_write(codec, 0x0000", imac_tdm)


if __name__ == "__main__":
    unittest.main(verbosity=2)
