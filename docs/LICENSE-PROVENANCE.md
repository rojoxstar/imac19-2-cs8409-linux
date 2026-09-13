# License provenance

The production files derive from the Linux CS8409 HDA codec driver.

| Files | SPDX license |
|---|---|
| `driver/source/cs8409-tables.c` | `GPL-2.0-only` |
| `driver/source/cs8409.c`, `cs8409.h` | `GPL-2.0-or-later` |
| external-build support headers | `GPL-2.0`, `GPL-2.0-only`, `GPL-2.0+`, or `GPL-2.0-or-later` as marked |
| external-module Makefile | `GPL-2.0` |

Because the combined driver includes a GPL-2.0-only file, the repository is
distributed as GPL-2.0-only. Each copied source file retains its original
SPDX identifier and copyright notice. The root `LICENSE` contains the GNU
General Public License version 2 text; `KERNEL-GPL-2.0-SPDX.txt` preserves the
Linux kernel’s SPDX license metadata and usage guidance.

No proprietary Apple, Boot Camp, or Cirrus binary/package content is included.
Apple artifacts were used only as static research inputs and are not
redistributed. The public Apple-DSP reports and mathematical descriptions are
independently authored clean-room material. Hashes, UUIDs, symbol names, and
addresses are factual provenance metadata; they do not reproduce executable
content. No conclusion here makes a broader legal claim about third-party use
of independently obtained Apple artifacts.
