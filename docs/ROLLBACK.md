# Rollback policy

Rollback is an identity-controlled transaction, not “try an older module.”

Before any future installation, preserve the exact currently installed
module outside `/lib/modules` with a manifest containing:

- SHA256, size, `srcversion`, `vermagic`, license and `module_layout`;
- running kernel and original resolved module path;
- creation time and transaction purpose.

Validate that backup before replacement and never overwrite a conflicting
backup. Restore by same-filesystem staging and atomic rename, verify the exact
target, then update dependency metadata and initramfs only for the target
kernel. Never reboot automatically.

For a failed V10 experiment, the rollback target is the exact module present
immediately before that experiment. Do not silently choose stock, V9, V8, or
another historical artifact. If identity or recovery is incomplete: stop and
do not reboot.
