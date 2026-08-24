# Production driver source

This directory is the exact V10 source tree used for the validated external
module. `source/cs8409.c`, `source/cs8409.h`, and
`source/cs8409-tables.c` have the hashes recorded in
`V10-SOURCE-MANIFEST.sha256`.

The support headers are copied from the matching Linux 7.0.0-30 source tree
solely to support an isolated external-module build. Their original SPDX
headers are preserved.

The canonical in-tree patch is
`patches/linux-7.0.0-30-imac19-2-v10.patch`. It was generated against the
pristine files from Ubuntu package `linux-source-7.0.0` version
`7.0.0-30.30`.

V11 diagnostic logging is not present here. It is preserved separately as a
patch under `diagnostics/`.
