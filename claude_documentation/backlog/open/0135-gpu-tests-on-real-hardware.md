# T0135 — The gpu bucket must run on real hardware, and say which device it used

| | |
|---|---|
| **Status** | 🔜 TODO |
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

- [ ] `zig build test -Dtest=gpu` uses a hardware adapter when one is reachable,
      without the developer remembering environment variables
- [ ] The suite **reports the adapter it ran on**, in output that survives the
      build summary — a run on llvmpipe must be visibly a run on llvmpipe
- [ ] Forcing the D3D12 gallium path is **conditional on WSL** and never applied
      on a host with a native DRM driver, where it would be wrong
- [ ] `05-verification-status.md` records which backends have hardware coverage
      and which do not
- [ ] Whether a software-only run should *fail* rather than pass is decided and
      recorded — see the open question below

## Subtasks

- [ ] 135.1 Detect the WSL D3D12 path in `build.zig` (`/dev/dxg` present,
      `/dev/dri` absent) and set `GALLIUM_DRIVER=d3d12` for the gpu bucket only
- [ ] 135.2 A `-Dgpu-adapter=<substring>` option feeding
      `MESA_D3D12_DEFAULT_ADAPTER_NAME`, because a laptop has two GPUs and the
      default is the wrong one
- [ ] 135.3 Surface the adapter string in the test output and in the run name
- [ ] 135.4 Record hardware-coverage status per backend in
      `05-verification-status.md`
- [ ] 135.5 Decide the software-fallback policy (see below)

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

### CI is unaffected, and deliberately

`build.zig` already builds the gpu bucket under `all` and runs it only when named
— because the Windows CI host has a desktop session, reaches Microsoft's GDI
generic OpenGL 1.1, and the process died before flushing stdout (exit code 5, no
doctest banner). **None of this ticket changes that.** CI runners have no GPU;
building the bucket is the half CI can honestly do. This ticket is about the
developer machine, where the hardware exists and was not being used.
