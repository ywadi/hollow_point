# hp_editor

The HollowPoint editor. A consumer of the engine library (`hp::engine`), not
part of it — see T0013 and D12.

Still a skeleton in most respects — there are no panels (T0032), no viewport
panel (T0033), no hierarchy or inspector (T0035), and no picking (T0173). What it
does have is a **camera you can move** and a way to **open a model**, which is
T0172 and which existed for one reason: without them the only way to look at the
renderer's output from a second angle was to write a gpu test that dumps a PNG.

## Running it

```sh
./build/linux-x86_64-release/apps/editor/hp_editor                  # the rock cube sample
./build/linux-x86_64-release/apps/editor/hp_editor path/to/car.glb  # any glTF or GLB
./build/linux-x86_64-release/apps/editor/hp_editor model.glb --frames=120
```

| Flag | Meaning |
|---|---|
| `<path>` | A glTF or GLB to open. Its directory is mounted in the VFS (**D13**) and the file is loaded through it, so a loose file and a file inside a pack take the same path. **The sample gameplay modules are not loaded when a model is named**, because `rockcube` would build a scene in front of it |
| `--frames=N` | Quit after N frames. **Use this.** A GUI left running on somebody's desktop is the failure mode, and a bounded run is what makes a change here checkable from a script |
| `--backend=vulkan` | Pins the backend request for a bug report. Vulkan is the only one (**D29**) |

## Camera

| Gesture | Does |
|---|---|
| Right-drag | Look |
| `W` `A` `S` `D` | Fly, in the plane you are looking along |
| `Q` `E` | Down / up |
| Shift, Ctrl | 4x and 16x speed |
| Alt + left-drag | Orbit around the pivot |
| Middle-drag | Pan; the pivot comes with you |
| Wheel | Zoom while orbiting, fly speed otherwise |
| `F` | Frame the opened model again |

**The left button is deliberately free.** Picking (T0173) takes it, and an editor
that steals it for the camera has to be unpicked later.

**None of the camera maths is ours.** `Diligent::FirstPersonCamera` from
`DiligentSamples/SampleBase` does the work — **D40**, we add, we do not rebuild.
`src/EditorCamera.hpp` is the seam, and its header comment is the thing to read
before changing anything about the camera: Diligent's camera space is
left-handed and this engine's is right-handed (**T0165**), and that difference
surfaces in exactly three places, all of them named there and each one measured
rather than reasoned about. `A` and `D` are crossed on purpose; that is one of
the three.
</content>
