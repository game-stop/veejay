# VeeJay binary release targets

This matrix covers Linux binary packages. ESP32 is intentionally excluded; ESP32 users build the supported source subset for their board and SDK.

## Release matrix

| Release target | Debian architecture | RPM architecture | Arch port | Configure CPU target | ISA policy |
|---|---|---|---|---|---|
| `amd64-generic` | `amd64` | `x86_64` | `x86_64` | `generic` | x86-64 baseline; runtime-dispatched isolated AVX2 routines remain available |
| `amd64-x86-64-v3` | `amd64` | `x86_64` | `x86_64` | `x86-64-v3` | global x86-64-v3 target, including AVX2 |
| `armhf-generic` | `armhf` | community builder | `armv7h` | `generic` | ARMv7 hard-float VFPv3-D16 baseline without a required NEON backend |
| `armhf-cortex-a7-neon` | `armhf` | community builder | `armv7h` | `cortex-a7` | global Cortex-A7 target with compile-time NEON backend when compiler probe succeeds |
| `arm64-generic` | `arm64` | `aarch64` | `aarch64` | `generic` | ARMv8-A baseline ASIMD |
| `arm64-cortex-a72` | `arm64` | `aarch64` | `aarch64` | `cortex-a72` | global Cortex-A72 target; ASIMD is still baseline |
| `powerpc-generic` | `powerpc` | community builder | `powerpc` | `generic` | scalar PowerPC fallback |
| `powerpc-7400-altivec` | `powerpc` | community builder | `powerpc` | `7400` | global 7400 target with compile-time AltiVec backend |
| `ppc64-generic` | `ppc64` | community builder | `powerpc64` | `generic` | scalar 64-bit big-endian PowerPC fallback |
| `ppc64-power7-altivec` | `ppc64` | community builder | `powerpc64` | `power7` | global POWER7 target with compile-time AltiVec backend |
| `ppc64le-generic` | `ppc64el` | `ppc64le` | `powerpc64le` | `generic` | portable little-endian PowerPC64 build |
| `ppc64le-power8-altivec` | `ppc64el` | `ppc64le` | `powerpc64le` | `power8` | global POWER8 target with compile-time AltiVec backend |

Debian directly supports `amd64`, `armhf`, `arm64`, and `ppc64el`; `powerpc` and big-endian `ppc64` use Debian Ports. Fedora-family RPM builders directly cover `x86_64`, `aarch64`, and `ppc64le`; 32-bit ARM and big-endian PowerPC require a community RPM repository/configuration. Arch officially targets `x86_64`; the ARM entries use Arch Linux ARM and the PowerPC entries use ArchPOWER or another compatible community builder.

## CPU dispatch policy

`--with-arch-target=generic` is the redistributable default. `--with-arch-target=native` is only for a local build because it records the build machine's CPU capabilities. A named CPU or ISA target is compiler-validated and applied globally as `-march=...` on x86 or `-mcpu=...` on ARM and PowerPC.

On x86-64, generic packages define the baseline SSE/SSE2/CMOV paths. The compiler is also probed for isolated functions using `__attribute__((target("avx2")))`; libav's `av_get_cpu_flags()` selects those routines at runtime in YUV conversion and chroma subsampling. `libvjmem` also checks libav CPU flags before accepting or benchmarking a memory implementation. The `x86-64-v3` package is still useful for compiler optimization outside those isolated routines, but the entire binary then requires an x86-64-v3 CPU.

On AArch64, ASIMD is part of the architecture baseline and is enabled in the generic package. On 32-bit ARM, the generic packages use the Debian-compatible ARMv7 hard-float VFPv3-D16 baseline; NEON is enabled only when the selected non-generic target passes the compiler probe. The foreign Arch builder overrides Arch Linux ARM's default `-mfpu=neon` for the generic profile so the package label and actual ISA requirement agree. ARM NEON/ASIMD routines are compile-time binary choices rather than per-call runtime dispatch.

On PowerPC, generic packages retain scalar fallbacks. ArchPOWER's PPC64LE distribution baseline is POWER8, so its foreign builder overrides the VeeJay generic profile to `-mcpu=powerpc64`; this keeps VeeJay's own generic code scalar even though the surrounding distribution still requires a POWER8-class machine. A non-generic target enables AltiVec only after a compiler probe; `libsubsample` then selects its AltiVec backend at compile time. Keep specialized ARM and PowerPC packages in profile-labelled repositories because they can fault on an older CPU.

## Building one target

List the matrix:

```bash
./build-binary-packages.sh --list-targets
```

Build one package ecosystem and collect outputs under `release-packages/YYYY-MM-DD/<release-target>/`:

```bash
./build-binary-packages.sh --target deb --release-target amd64-generic \
  --prepare-sbuild SBUILD_CHROOT_MODE=unshare

./build-binary-packages.sh --target rpm --release-target amd64-x86-64-v3

./build-binary-packages.sh --target arch --release-target arm64-cortex-a72 \
  ARCH_CHROOT_DIR=/path/to/an/aarch64/archbuild/root
```

Later packages receive earlier packages from the same profile directory, so Core and server development dependencies are available inside clean sbuild, mock, and makechrootpkg roots. Each target directory contains `BUILD-PROFILE.txt` and `SHA256SUMS`.

Package work directories are isolated by release target. After repairing or restarting a long suite, resume at a package root without rebuilding earlier packages:

```bash
./build-binary-packages.sh --target deb --release-target arm64-generic \
  --from veejay-server SBUILD_CHROOT_MODE=unshare
```

Use one clean builder of the matching architecture, or a correctly configured QEMU/binfmt builder. For Debian Ports, the named targets select `sid` and `http://ftp.ports.debian.org/debian-ports`; pass the current archive keyring through `SBUILD_CREATE_ARGS=--keyring=/path/to/debian-ports-archive-keyring.gpg` when creating the chroot. For community RPM and Arch ports, pass the repository-specific `MOCK_CONFIG` or `ARCH_CHROOT_DIR` explicitly.

Current Debian Ports `powerpc` and `ppc64` package scripts require a recent user-mode emulator. QEMU 8.2 aborts while configuring the current `systemd` package; QEMU 11.1.1 has been validated for both big-endian targets.

When `SBUILD_CONFIG` points to the supplied `sbuild-ports.conf`, the 7400 and POWER7 targets also export a matching `QEMU_CPU` into the clean build. This is required to execute their compile-time AltiVec tests under emulation; QEMU's default 32-bit PowerPC model does not implement AltiVec.

The current Debian Ports PowerPC FFmpeg packages depend on `libx265-216`, but that binary is absent from the PowerPC package index. PowerPC release directories therefore include a locally staged `libx265-216` package alongside the VeeJay packages; specialized PowerPC profiles must seed the same package before building Core.

Generic and specialized packages currently share package names and versions. Publish them in separate repositories or artifact directories and never mix both profiles in one dependency repository.

The RPM specs request FFmpeg through `pkgconfig(libavcodec)`, `pkgconfig(libavformat)`, `pkgconfig(libavutil)`, `pkgconfig(libswscale)`, and `pkgconfig(libswresample)` capabilities. Fedora 43 provides these through its first-party `ffmpeg-free-devel` split on PPC64LE; RPM Fusion Free provides the full FFmpeg implementation used by the AMD64 and AArch64 builders. Fedora and ArchPOWER do not supply Pure Data development headers, so their suites contain the six Autotools packages unless an additional community Pd repository or separately packaged Pd SDK is provided. Debian and Arch Linux ARM provide the required Pd development files directly.

For the supplied foreign Arch runner, use `ARCH_INCLUDE_PUREDATA=0` on ArchPOWER. The runner then builds Core, server, client, utils, Eidolon, and Director and skips only `sendVIMS`.

## Package suite

The orchestrated binary suite is built in dependency order:

1. `veejay-core`
2. `veejay-server`
3. `veejay-client`
4. `veejay-utils`
5. `veejay-eidolon`
6. `veejay-director`
7. `sendVIMS` (`veejay-puredata` package)

The first six projects consume `VEEJAY_ARCH_TARGET`; `sendVIMS` has no architecture-tuning configure option and is compiled with the target distribution's normal compiler flags.
