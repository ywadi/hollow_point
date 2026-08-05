# T0135 — The gpu bucket must run on real hardware, and say which device it used

| | |
|---|---|
| **Status** | ✅ DONE |
| **Priority** | High |
| **Complexity** | Simple |
| **Phase** | 4 — Render layer |
| **Order** | 386 |
| **Created** | 2026-08-05 |
| **Blocked by** | Nothing |
| **Refs** | [../completed/0027-render-stack.md](../completed/0027-render-stack.md), [../completed/0028-scene-draw-submission.md](../completed/0028-scene-draw-submission.md), [../completed/0134-pbr-renderer-adoption.md](../completed/0134-pbr-renderer-adoption.md), [../completed/0046-frame-render-targets.md](../completed/0046-frame-render-targets.md), [../../documentation/05-verification-status.md](../../documentation/05-verification-status.md), T0003 |

## Why

**Every `gpu` test written so far ran on a software rasteriser, and two closed
tickets claimed otherwise.** T0027 said "a real GPU"; T0028 said "Vulkan on an
NVIDIA RTX 4070". Neither was checked. Both are corrected now, but the reason
this is a ticket rather than a correction is that **nothing in the harness makes
the mistake hard to repeat** — a green `gpu` bucket looks like hardware coverage
and silently is not.

That is the failure mode `CLAUDE.md` names directly: *"a filter lies in both
directions"*, and *"when a check fails, suspect the check"*. This one did not
even fail. It passed, on the wrong device, and reported success.

## Done when

- [x] `zig build test -Dtest=gpu` uses a hardware adapter when one is reachable,
      without the developer remembering environment variables — **verified: no
      environment variables at all on this host**, both backends on an RTX 2080
- [x] The suite **reports the adapter it ran on**, in output that survives the
      build summary — a run on llvmpipe must be visibly a run on llvmpipe —
      **verified both ways**: hardware prints `[hardware]`, a forced lavapipe
      prints `[SOFTWARE RASTERISER]`
- [~] Forcing the D3D12 gallium path is **conditional on WSL** and never applied
      on a host with a native DRM driver, where it would be wrong — **the
      protective half is verified** (it correctly declines here, on a DRM host);
      the WSL-positive half is built but has not been executed on the laptop
- [x] `05-verification-status.md` records which backends have hardware coverage
      and which do not — rewritten as a per-host table
- [x] Whether a software-only run should *fail* rather than pass is decided and
      recorded — always report, `-Dgpu-require-hardware` to enforce

## Subtasks

- [~] 135.1 Detect the WSL D3D12 path in `build.zig` (`/dev/dxg` present,
      `/dev/dri` absent) and set `GALLIUM_DRIVER=d3d12` for the gpu bucket only
      — **built (`wslGalliumNeeded`); only the negative branch is verified**,
      because this machine is not WSL. It correctly declines here. The
      positive branch needs a run on the laptop
- [~] 135.2 A `-Dgpu-adapter=<substring>` option feeding
      `MESA_D3D12_DEFAULT_ADAPTER_NAME` — **built and registered**, but
      **unverifiable on this machine**: it only bites on the WSL D3D12 path,
      which does not exist here
- [x] 135.3 Surface the adapter string in the test output — banner verified on
      both backends, red and green. **Not** in the run name: the name is fixed
      when the build graph is built and the adapter is only known after a
      device exists, so putting it there would mean probing the GPU at
      configure time. The banner meets the Done-when; the run name cannot
- [x] 135.4 Record hardware-coverage status per backend in
      `05-verification-status.md` — rewritten with both machines
- [x] 135.5 Decide the software-fallback policy — **decided**: always report,
      `-Dgpu-require-hardware` to enforce. Reasoning and the two rejected
      alternatives are recorded below

## Notes / findings

### Diagnosed 2026-08-05 — measured on this machine, not looked up

The machine has an **RTX 4070 Laptop GPU** and an **AMD Radeon 780M** iGPU.
`nvidia-smi` works (`/usr/lib/wsl/lib/nvidia-smi`), `/dev/dxg` exists and is
world-readable, WSLg is up (`DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`), and no
`LIBGL_*`/`MESA_*`/`VK_*` variable forces software. So the hardware was never the
problem.

**Vulkan: no hardware path exists here, and this is not fixable by configuration.**

- `/usr/share/vulkan/icd.d/` contains only Mesa ICDs — `asahi`, `gfxstream_vk`,
  `intel`, `intel_hasvk`, `lvp`, `nouveau`, `radeon`, `virtio`. The only one that
  loads is **`lvp` (lavapipe)**, which is software.
- **NVIDIA's WSL driver package ships no Vulkan ICD.** `/usr/lib/wsl/lib/` carries
  `libd3d12.so`, `libd3d12core.so`, `libdxcore.so`, `libnvwgf2umx.so` and the CUDA
  stack — DirectX and compute, not Vulkan. `find / -name "nvidia_icd*.json"`
  returns nothing.
- **Mesa's Dozen (`dzn`), which translates Vulkan to D3D12, is not in Ubuntu's
  `mesa-vulkan-drivers`** (25.2.8). `find / -name "dzn_icd*.json"` returns nothing.

So `RenderBackend::Vulkan` — and `Default`, which prefers Vulkan — resolve to
lavapipe, and there is nothing to select instead.

**OpenGL: hardware works today, with zero installs.** `d3d12_dri.so` is present,
which is Mesa's OpenGL-on-D3D12 gallium driver and the standard WSL hardware
path. It is not auto-selected because **`/dev/dri` does not exist** — there is no
DRM render node, so Mesa's normal device probe finds nothing and falls back to
`swrast`. It has to be named:

```sh
GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
```

Measured, three ways:

```
default:                      llvmpipe (LLVM 20.1.2, 256 bits)
GALLIUM_DRIVER=d3d12:         D3D12 (AMD Radeon 780M Graphics)      <- the iGPU
  + MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA:
                              D3D12 (NVIDIA GeForce RTX 4070 Laptop GPU)
```

**The adapter hint is not optional on this machine** — without it Mesa picks the
integrated Radeon, which is hardware but not the one anyone means.

### The engine's own gpu bucket, run on the discrete GPU

```
OpenGL on 'D3D12 (NVIDIA GeForce RTX 4070 Laptop GPU)'
Vulkan on 'llvmpipe (LLVM 20.1.2, 256 bits)'

[doctest] test cases:  11 |  11 passed | 0 failed | 0 skipped
[doctest] assertions: 477 | 477 passed | 0 failed |
```

**Pixel-identical to the software run**, which is a real result and worth
recording as one:

```
world only: left (0, 0, 0), right (0, 0, 255)
stacked:    left (0, 0, 0), right (0, 0, 0)
pixels differing from the clear colour: 65536 of 65536
```

So T0027's compositing, T0028's reverse-Z draw and T0134's frame-attribs fix now
have **hardware evidence on the OpenGL backend**, and the reverse-Z depth
comparison in particular is confirmed on a real NVIDIA driver rather than only on
lavapipe.

### 2026-08-05 — the diagnosis above is one machine's, and the second machine inverts it

**Everything under "Diagnosed 2026-08-05" was measured on a WSL laptop. This
ticket is now being worked on a different machine, and almost every fact flips.**
That is not a correction — both are true, on their own hardware — but it means
the harness must be *host-conditional*, and it is why the `/dev/dxg` + `/dev/dri`
probe in 135.1 is the right shape rather than an environment variable someone
sets once and forgets.

Measured here, not looked up:

| | WSL laptop (the notes above) | This machine |
|---|---|---|
| WSL | yes | **no** — Pop!_OS, `6.16.3-76061603-generic`, bare metal |
| `/dev/dri` | absent | **present** — `card0`, `renderD128` |
| `/dev/dxg` | present | **absent** |
| Vulkan ICDs | Mesa only; `lvp` the only one that loads | **`nvidia_icd.json` present** |
| Vulkan resolves to | lavapipe (software) | **NVIDIA GeForce RTX 2080** (hardware) |
| OpenGL resolves to | `swrast` unless `GALLIUM_DRIVER=d3d12` | **NVIDIA GeForce RTX 2080** natively |
| GPU | RTX 4070 Laptop + Radeon 780M | RTX 2080 |
| Env vars needed | two | **none** |

Evidence for the two adapter strings, from the engine's own `describeAdapter`
and from Mesa respectively:

```
[info ] render: Vulkan on 'NVIDIA GeForce RTX 2080', 1280x720, 3 buffers, vsync on
OpenGL renderer string: NVIDIA GeForce RTX 2080/PCIe/SSE2
```

**Two consequences for this ticket:**

- **135.1's probe is confirmed correct by the negative case.** Done-when 3 asks
  that the D3D12 gallium path never be forced "on a host with a native DRM
  driver, where it would be wrong". This machine *is* that host, and the
  `/dev/dxg` present + `/dev/dri` absent rule correctly declines to fire here.
  The rule now has a real machine on each side of it rather than one.
- **The asymmetry that made 135.5 hard does not exist here.** The notes above
  argue a "fail on software" policy "would fail every Vulkan gpu test here
  permanently". True on the laptop; **false on this machine**, where Vulkan has
  a hardware ICD. So the policy cannot be derived from either machine alone —
  which is itself the argument for a per-backend, host-conditional answer rather
  than a global one.

### The open question this ticket must decide

**Should a gpu run that lands on a software rasteriser pass, skip, or fail?**

It currently passes, silently, and that is what produced two false claims. The
three options each cost something:

- **Pass quietly** — today's behaviour. Cheap, and it lies.
- **Pass but say so loudly**, e.g. a `MESSAGE` naming the adapter plus a line in
  the summary. Honest, and easy to scroll past — though `05-verification-status.md`
  would carry the standing answer.
- **Fail unless a hardware adapter is present**, with an explicit
  `-Dgpu-allow-software` escape. Strongest, and it makes the bucket unusable for
  anyone without a GPU — including the Vulkan half **on this very machine**,
  which has no hardware path at all.

The Vulkan asymmetry is what makes this a real decision rather than an obvious
one: a policy of "fail on software" would fail every Vulkan gpu test here
permanently, on a machine that has a perfectly good GPU. A per-backend policy is
probably the answer, and that is exactly what needs writing down.

### 135.5 decided 2026-08-05 — always report; fail only when asked

**The bug is invisibility, not permissiveness, and the fix has to attack the
thing that actually failed.** T0027 and T0028 made false hardware claims because
the run printed nothing about the adapter — not because the run was allowed to
proceed. So the mandatory half is the banner: **every gpu run prints its backend,
its adapter and whether that adapter is software**, unconditionally.

The enforcement half is opt-in: **`-Dgpu-require-hardware` turns a software
adapter into a failure.** That is the flag to use before closing a rendering
ticket, and CI never sets it.

**Fail-by-default is rejected, and the second machine is why.** It would be
unusable across the two hosts this project is actually developed on: the WSL
laptop has no Vulkan hardware path at all — no NVIDIA ICD, no Dozen, nothing to
install — so `-Dtest=gpu` would fail there permanently for half the backends on a
machine with a perfectly good GPU. A bucket a developer cannot run is a bucket
they stop running, and then it rots quietly, which is a worse version of the
problem this ticket exists to fix.

**A per-backend policy baked into the build is also rejected**, though the notes
above guessed at it. Encoding "Vulkan may be software on this host" into
`build.zig` writes one machine's driver situation into the repository, and it
would be wrong on this machine the day it was committed. The per-run flag gets
the same outcome without the staleness: on the laptop,
`-Dtest=gpu -Dgpu-require-hardware` correctly reports OpenGL hardware and a
Vulkan failure — and that failure is *true and informative*, because there
genuinely is no Vulkan hardware there.

### Built and measured 2026-08-05 — evidence

`build.zig` (`wslGalliumNeeded`, `-Dgpu-adapter`, `-Dgpu-require-hardware`) and
`tests/gpu/gpu_adapter_report_test.cpp`. The bucket is picked up by CMake's glob,
so no build file changed for the new suite (T0012's "adding a test needs one
file", still true).

**Green — hardware on both backends, no environment variables:**

```
$ zig build test -Dtest=gpu -Dtarget=linux-x86_64
[hp gpu] ---- adapters this run (T0135) ----
[hp gpu] Vulkan  -> NVIDIA GeForce RTX 2080  [hardware]
[hp gpu] OpenGL  -> NVIDIA GeForce RTX 2080/PCIe/SSE2  [hardware]
[hp gpu] (informational -- pass -Dgpu-require-hardware to enforce)
[doctest] test cases:  14 |  14 passed | 0 failed | 0 skipped
[doctest] assertions: 652 | 652 passed | 0 failed |
EXIT: 0
```

**This is the project's first hardware evidence on the Vulkan backend.** Every
prior gpu run, on the machine these notes were first written on, was lavapipe.

**Red — the enforcement actually fails, verified by forcing lavapipe:**

```
$ VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  zig build test -Dtest=gpu -Dtarget=linux-x86_64 -Dgpu-require-hardware
[hp gpu] Vulkan  -> llvmpipe (LLVM 15.0.7, 256 bits)  [SOFTWARE RASTERISER]
gpu_adapter_report_test.cpp:179: ERROR: -Dgpu-require-hardware was passed and
  Vulkan came up on a software rasteriser (llvmpipe (LLVM 15.0.7, 256 bits))
[doctest] test cases:  14 |  13 passed | 1 failed | 0 skipped
EXIT: 1
```

Red-green rather than a pass observed once — which is this repository's rule for
a check, and doubly so for a check whose whole purpose is that the last one lied.

### What is built but not verified, and why

- **The WSL positive branch.** `wslGalliumNeeded` returns false here and that is
  correct, but the true branch — and therefore `GALLIUM_DRIVER=d3d12` and
  `-Dgpu-adapter` actually changing an adapter — has not been executed. It needs
  a run on the laptop. The negative case is the one that protects this machine,
  and it is verified.
- **Software classification on OpenGL.** Proven on Vulkan only.
  `LIBGL_ALWAYS_SOFTWARE=1` is a Mesa variable and **NVIDIA's proprietary GL
  driver ignores it**, so the GL adapter stayed on hardware through the red run.
  `looksLikeSoftware` is shared by both paths, so the logic is exercised; the
  GL-specific end of it is not.
- **`llvmpipe (LLVM 15.0.7)`** here versus `20.1.2` in the notes above. Two
  different machines with different Mesa builds, not a regression.

### Deliberately not done here

`bringUp`/`tearDown` are now duplicated in **seven** files in `tests/gpu/`, this
ticket having added the seventh. Collapsing them into a shared header is worth
doing and was not done: T0135 is about what the harness *reports*, and rewriting
six working gpu suites would put unrelated risk in the same change. Recorded so
it is a decision rather than something nobody noticed.

### CI is unaffected, and deliberately

`build.zig` already builds the gpu bucket under `all` and runs it only when named
— because the Windows CI host has a desktop session, reaches Microsoft's GDI
generic OpenGL 1.1, and the process died before flushing stdout (exit code 5, no
doctest banner). **None of this ticket changes that.** CI runners have no GPU;
building the bucket is the half CI can honestly do. This ticket is about the
developer machine, where the hardware exists and was not being used.
