# Build and installation

## Safety boundary

The repository does not publish a module binary or a general-purpose
installer. Building is offline. Installing a codec module is a separate,
machine-mutating operation and must be reviewed for the exact target.

The only tested target is:

```text
kernel:    7.0.0-30-generic
machine:   iMac19,2
subsystem: 106b:0f00
```

## Offline build

```sh
./scripts/verify-source.sh
./scripts/run-offline-tests.sh
./scripts/build-v10.sh
./scripts/verify-module.sh
```

The build script refuses another kernel, verifies source hashes, builds with
`W=1`, and checks `srcversion`, `vermagic`, license and `module_layout`. It
does not copy into `/lib/modules`, run `depmod`, update initramfs, load a
module, or reboot.

## Why no public installer is included

The validated private installer was a fail-closed transaction from one exact
V9 binary to one exact V10 binary. Publishing it as a generic installer would
encode a false rollback assumption and could overwrite an unrelated module.

A future public installer needs its own review and disposable transaction
tests. At minimum it must:

1. verify the exact running kernel, DMI model, subsystem and current module;
2. refuse symlink or duplicate-module ambiguity;
3. create and independently validate an external backup from the actual
   installed target;
4. stage a verified candidate beside the target on the same filesystem;
5. replace atomically;
6. run `depmod` and initramfs update only for the target kernel;
7. stop without reboot;
8. restore only the verified immediate backup on handled failure.

Do not treat these requirements as authorization or paste them into a root
shell. System S3 remains unsupported after installation.
