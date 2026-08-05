# Gameplay authoring — what game code looks like

**Nothing in this document is built.** It is the agreed *shape* of the gameplay
API, written down after a long design conversation so the conclusions are not
lost and do not have to be re-derived. Every code sample is a proposal. The
decision behind it is **D23** in
[`02-decision-log.md`](02-decision-log.md); the tickets that build it are
[T0062](../backlog/open/0062-entity-behaviours.md) (behaviours and the layer)
and [T0076](../backlog/open/0076-autoloads.md) (services).

Owned by **T0062**. If the code and this document ever disagree, fix both in the
same commit.

---

## The goal, stated so it can be judged

A gameplay programmer should write **ordinary C++ classes** and see **no engine
plumbing**. The target is GDScript's ergonomics without GDScript — same shape,
same callback names, same mental model, one extra line of ceremony.

The measure is concrete: the door below should read within one line of its
GDScript equivalent, and a gameplay file should contain no `adoptMetaContext`,
no `forgetType`, no `HP_EXPORT`, no `extern "C"`, no `entt::` anything, and no
`ModuleContext`.

---

## Three tiers, one concept

Godot has autoloads, scene-root scripts and node scripts. So does this, and they
are **one mechanism with three lifetimes** rather than three mechanisms.

| Tier | Godot equivalent | Type | Lifetime | Config lives in |
|---|---|---|---|---|
| **Game** | autoload | `hp::Service`, `Scope::Session` | one play session | project file (T0024) |
| **Scene** | root node script | `hp::Service`, `Scope::Scene` | scene load → unload | scene file (T0022) |
| **Entity** | node script | `hp::Behaviour` | entity create → destroy | scene file, on the entity |

Same callbacks at every tier. The only differences are how long it lives and
where its configuration is stored.

**"Session" means one play-mode run, not the editor's lifetime.** This is the
subtlest trap in the design and [T0076](../backlog/open/0076-autoloads.md)
already recorded it: a `GameState` created when the project opens and reused
across playtests means every test run starts from the previous one's state — a
bug that only appears on the *second* run. Session services are created on play
entry and destroyed on stop.

---

## The callbacks, and where they run

Named after Godot's, mapped onto the phases in
[`08-frame-anatomy.md`](08-frame-anatomy.md). **The vocabulary is Godot's; the
storage model is not** — see D23.

| Callback | Frame phase | Notes |
|---|---|---|
| `ready()` | on construction | Godot's `_ready`. Backed by `entt::on_construct`. |
| `physicsProcess(dt)` | 3b — fixed step | Reproducible work. Runs 0..n times per frame. **Never LOD this.** |
| `process(dt)` | 4 — variable update | Godot's `_process`. The default place for gameplay. |
| `lateProcess(dt)` | 8 — late update | Followers, cameras, attachment points. **Godot has no equivalent** — see below. |
| `destroyed()` | on destruction | Backed by `entt::on_destroy`. |

**`lateProcess` is the phase Godot lacks**, and it exists for a specific bug that
`08-frame-anatomy.md` documents: a camera updating alongside what it follows
reads this frame's or last frame's position depending on registration order,
producing jitter that profiles as nothing. Follow logic in `process()` is a bug
even when it appears to work.

---

## A door

```cpp
// Door.h — the base. Owns the open/close state machine. Subclasses fill in the
// only two things that actually differ between doors.
#include <hp/Gameplay.hpp>

class Door : public hp::Behaviour {
public:
    enum class State { Closed, Opening, Open, Closing };

    float openSpeed      = 2.0f;    // full travel in 1/openSpeed seconds
    bool  autoClose      = false;
    float autoCloseDelay = 3.0f;

    hp::Signal<Door*> opened;
    hp::Signal<Door*> closed;
    hp::Signal<Door*> refused;

    void open() {
        if (state_ == State::Open || state_ == State::Opening) return;
        if (!canOpen()) { refused.emit(this); onRefused(); return; }
        state_ = State::Opening;
    }

    void close() {
        if (state_ == State::Closed || state_ == State::Closing) return;
        state_ = State::Closing;
    }

    void toggle() { isOpen() ? close() : open(); }

    bool  isOpen() const { return state_ == State::Open; }
    State state()  const { return state_; }

    void process(double dt) override {
        if (state_ == State::Closed || state_ == State::Open) return;  // nothing to do

        const float dir = (state_ == State::Opening) ? 1.0f : -1.0f;
        travel_ = std::clamp(travel_ + dir * openSpeed * float(dt), 0.0f, 1.0f);

        applyMotion(travel_);                       // the subclass hook

        if (travel_ >= 1.0f) {
            state_ = State::Open;
            opened.emit(this);
            if (autoClose) after(autoCloseDelay, [this] { close(); });
        } else if (travel_ <= 0.0f) {
            state_ = State::Closed;
            closed.emit(this);
        }
    }

protected:
    /// Override to add a condition. Default: always allowed.
    virtual bool canOpen() { return true; }

    /// Override to react to a refusal — a sound, a shake.
    virtual void onRefused() {}

    /// Every concrete door must say how it moves. `t` is 0 (shut) to 1 (open).
    virtual void applyMotion(float t) = 0;

private:
    State state_  = State::Closed;
    float travel_ = 0.0f;
};

HP_BEHAVIOUR_BASE(Door, openSpeed, autoClose, autoCloseDelay)
```

`HP_BEHAVIOUR_BASE` means *abstract, not placeable*. It also defines the group,
so `hp::each<Door>` finds every subclass.

Two concrete doors, one function each:

```cpp
// HingedDoor.cpp
class HingedDoor final : public Door {
    using Super = Door;
public:
    float openAngle = 90.0f;
protected:
    void applyMotion(float t) override { setRotation(hp::yaw(openAngle * t)); }
};
HP_BEHAVIOUR(HingedDoor, openAngle)
```

```cpp
// SlidingDoor.cpp
class SlidingDoor final : public Door {
    using Super = Door;
public:
    hp::float3 slideAxis   = {1, 0, 0};
    float      slideLength = 1.2f;

    void ready() override { shut_ = position(); }

protected:
    void applyMotion(float t) override {
        setPosition(shut_ + slideAxis * (slideLength * t));
    }
private:
    hp::float3 shut_{};
};
HP_BEHAVIOUR(SlidingDoor, slideAxis, slideLength)
```

### Composition where inheritance would trap you

`class LockedDoor : public HingedDoor` is the tempting next step and it is wrong
— you will immediately want a locked *sliding* door. "Locked" is not a kind of
door; it is a property some doors have.

```cpp
// Lock.cpp — a separate behaviour on the same entity
class Lock final : public hp::Behaviour {
public:
    hp::Name requiredKey = "brass_key";
    bool     locked      = true;

    bool tryUnlock() {
        if (!locked) return true;
        if (hp::service<Inventory>().has(requiredKey)) { locked = false; return true; }
        return false;
    }
};
HP_BEHAVIOUR(Lock, requiredKey, locked)
```

One override in the base makes **every** door type lockable, including ones
written later:

```cpp
// in Door
bool canOpen() override {
    if (Lock* lock = get<Lock>()) return lock->tryUnlock();
    return true;
}
```

**The rule worth writing on the wall: inherit for what a thing *is*, compose for
what it *has*.** A sliding door *is* a door. A locked door *has* a lock.

---

## The three tiers, in code

```cpp
// ---- game logic: survives scene changes ----
class GameState final : public hp::Service {
public:
    int lives = 3;
    int score = 0;
    void ready() override { log("new game"); }
};
HP_SERVICE(GameState, hp::Scope::Session, lives, score)
```

```cpp
// ---- scene logic: born and buried with the level ----
class LevelDirector final : public hp::Service {
public:
    int   waveCount = 5;
    float waveDelay = 20.0f;

    void ready() override { startWave(0); }
    void process(double dt) override {
        if ((timer_ -= dt) <= 0.0) startWave(++wave_);
    }
private:
    void startWave(int i) {
        hp::service<GameState>().score += 100;   // reaching *up* a tier is fine
        timer_ = waveDelay;
    }
    int wave_ = 0;
    double timer_ = 0.0;
};
HP_SERVICE(LevelDirector, hp::Scope::Scene, waveCount, waveDelay)
```

**Reach up, signal down.** A behaviour calling `service<GameState>()` is fine —
the thing it reaches outlives it. A `GameState` holding a `LevelDirector*` is
not, because the level dies first. Downward communication is a signal or a
query, never a stored pointer.

**Teardown runs inward-out:** scene services → entities → session services.
T0076 worked out the first step — scene services typically hold entity
references, so destroying entities first leaves them resolving dangling
references during their own teardown. `hp::Ref<T>` softens this considerably:
it is GUID-backed and resolves on use, so a stale reference returns null rather
than crashing. The ordering still matters; getting it wrong stops being fatal.

**One honest cost:** `hp::service<T>()` is ambient access. It resolves to a
pointer cached at load, so it is fast, and lifetime and teardown order are
compiler-guaranteed — but *"who touches the score?"* is a grep, not a call
graph. That is the same traceability cost **D10** records for the message bus,
paid deliberately here for ergonomics.

---

## Communication — three mechanisms, per D10

**D10** already decided the layering; this is what it looks like at this tier.

**Signals — an authored partner.** An airlock where both doors must never be
open at once:

```cpp
class Airlock final : public hp::Behaviour {
public:
    hp::Ref<Door> inner;
    hp::Ref<Door> outer;

    void ready() override {
        if (Door* i = inner.get()) i->opened.connect(this, &Airlock::onInnerOpened);
        if (Door* o = outer.get()) o->opened.connect(this, &Airlock::onOuterOpened);
    }
private:
    void onInnerOpened(Door*) { if (Door* o = outer.get()) o->close(); }
    void onOuterOpened(Door*) { if (Door* i = inner.get()) i->close(); }
};
HP_BEHAVIOUR(Airlock, inner, outer)
```

No `disconnect`. Connections keyed to a `Behaviour` are dropped when it dies —
that is the layer's job, and it is the fix for the dangling-callback hazard
T0068's review note already records for action callbacks.

**Direct call — a known target.**

```cpp
class Switch final : public hp::Behaviour {
public:
    hp::Ref<Door> target;
    void interact() { if (Door* d = target.get()) d->toggle(); }
};
HP_BEHAVIOUR(Switch, target)
```

**Group operation — all of them, subclasses included.**

```cpp
hp::each<Door>([](Door& d) { d.close(); });        // lockdown
hp::each<SlidingDoor>([](SlidingDoor& d) { ... }); // only the sliding ones
```

`hp::Ref<T>` is what makes *"do not store raw pointers to other behaviours or
entities across frames"* structural rather than a rule in a header nobody opens.

---

## The escape hatch — where the performance promise lives

When you have twenty thousand of something, skip the behaviour entirely:

```cpp
// Projectiles.cpp — no class, no virtual call, no per-object anything
struct Projectile { hp::float3 velocity; float life; };

HP_SYSTEM(projectiles, hp::Phase::FixedUpdate)
void projectiles(hp::Scene& scene, double dt) {
    for (auto [e, p, tf] : scene.each<Projectile, hp::Transform>()) {
        p.life -= float(dt);
        tf.position += p.velocity * float(dt);
        if (p.life <= 0.0f) scene.queueDestroy(e);
    }
}
```

Same file, same language, same build, full access to the registry. **This is the
ceiling a scripting layer could not give you** — a gameplay developer who needs
raw throughput drops one level without leaving the module or waiting on a
binding.

**The line: behaviours for things you would name individually, systems for
things you would count.** A door is named. A particle is counted.

---

## The performance model

### Behaviours are cheap here in a way they are not elsewhere

| | Cost per object per frame |
|---|---|
| Godot `_process` | script VM call — hundreds of ns |
| Unity `Update()` | native→managed transition **plus** a cache miss on a scattered heap object |
| **Here** | devirtualised direct call on a contiguous pool — **estimated ~2–5 ns** |

Two structural reasons: one entt pool per concrete type (contiguous,
prefetcher-friendly) and `final` on leaves (direct call, no vtable indirection).

**That number is arithmetic, not a measurement, and it is load-bearing** — the
"just always tick" decision below rests on it. T0062 owns measuring it.

T0062's original notes already reached the same conclusion: *"a virtual call is
a few nanoseconds, and ten thousand behaviours updating per frame is genuinely
fine. The real cost is cache misses from scattered allocations, which pooling by
type addresses."*

### Three tiers of ticking, and only the first is in the tutorial

1. **No `process()` override → not in any process pool.** Zero cost, zero
   thought, automatic. Most behaviours are here: a `Lock`, a `Health`, an
   `Inventory` are data plus methods.
2. **Override `process()` → called every frame.** Write the code, early-return
   when there is nothing to do. **This is the whole model a new developer
   learns.**
3. **`setProcess(false)` → an optimisation for the rare case.** It exists, it is
   documented, and it is deliberately *not* in the getting-started path.

Godot and Unity need their opt-out prominently because their per-object cost is
20–100× higher. Pooling is what buys the right to ignore it here.

### Tick LOD

Unreal ships this (`TickInterval` plus a Significance Manager); every crowd or
RTS game does some version of it. Distant units thinking at 2–5 Hz is often the
single largest CPU win in that genre.

```cpp
// Policy in a cheap system, not in the behaviour. Runs at 2 Hz, not 60 —
// deciding how often to think is not itself urgent.
HP_SYSTEM(aiTickLod, hp::Phase::Update, hp::EverySeconds(0.5))
void aiTickLod(hp::Scene& scene, double) {
    const auto eye = hp::activeCameraPosition(scene);
    hp::each<Enemy>([&](Enemy& e) {
        const float d = distance(e.worldPosition(), eye);
        e.setTickInterval(d < 20.f ? 0.0f : d < 60.f ? 0.1f : 0.5f);
    });
}
```

The behaviour says *what it does*; a system says *how often*. A designer tuning
LOD distances never opens `Grunt.cpp`.

**Three rules that make it correct:**

1. **`dt` is the accumulated delta**, not the frame delta. Pass 0.016 to
   something running at 5 Hz and everything moves 12× too slow — which presents
   as a physics bug, not a LOD bug.
2. **Nothing edge-triggered in a LOD'd `process()`.** `travel_ += speed * dt` is
   fine; "did the player just press X" is not — events fall between ticks.
3. **Never LOD `physicsProcess()`.** Reproducibility is the entire point of
   phase 3b; skipping steps makes it frame-rate dependent, which is the bug the
   fixed step exists to prevent.

**Two mechanisms, and they are not the same thing.** An *interval* saves the
body of `process()` but still visits every instance to check whether it is due.
*Bucketing* — partitioning the pool and ticking `frame % N` — saves the
traversal too. Given a ~2–5 ns dispatch, interval LOD only pays when the body
does real work (perception, pathfinding, target scans). For a door animation it
is pure overhead.

---

## What a gameplay developer never sees

`adoptMetaContext` · `forgetType` · `HP_EXPORT` · `HP_API` · `extern "C"` ·
`ModuleContext` · `entt::` anything · `registry()` · `setLocalTransform` vs a
raw transform write · `markTransformDirty` · the hot-reload snapshot · build ids
· the frame phase table.

Every one of those is a **silent** failure mode today —
[`06-engine-conventions.md`](06-engine-conventions.md) documents them next to
the thing that causes them, but a comment in `Reflect.hpp` protects nobody who
never opens `Reflect.hpp`. Hiding them is the point of the layer, not a
convenience.

**The rule that makes hiding safe: the layer may fail at compile time, never
silently at runtime.** An ugly template error is annoying; a door that does not
move for no reason is a lost afternoon.

## What a gameplay developer does have to learn

Four things. This is the honest size of the gap versus GDScript:

1. `HP_BEHAVIOUR(Door, openSpeed, openAngle)` — exported fields listed once.
   The `@export` tax. C++ cannot reflect field names, so something must list
   them.
2. `using Super = Enemy;` when deriving — this is how the engine learns the
   hierarchy, and without it `hp::each<Enemy>` will not find the subclass.
3. `final` on concrete behaviours, for the devirtualised call.
4. **A segfault is still a segfault.** GDScript prints a red line and keeps
   running; C++ does not. This is the one gap no layer closes, and it is the
   price of **D14**. Say it out loud to the team rather than letting them find
   it.

---

## Known gaps and open questions

Recorded rather than assumed. None of these blocks the design; all of them will
be asked.

- **The hot-reload snapshot is the hardest unbuilt piece.** Genuine module
  unload *works* (T0048/T0105.1, proven over 25 host lifetimes), which means
  vtables and module-instantiated entt pools genuinely dangle. Reflected
  snapshot → unload → load → restore is therefore **mandatory**, not optional,
  and it is one mechanism covering both behaviours (T0062.6) and services
  (T0076.8).
- **Scene services: `hp::Service` or a behaviour on a scene-root entity?**
  Genuinely balanced and not settled. As a behaviour you get the inspector,
  serialization and lifecycle free. As a service it stays out of the hierarchy,
  which T0076 argues for. Godot and Unity make it a node/GameObject; Unreal
  makes it a special actor the world spawns.
- **Additive scenes break `service<T>()` for the scene tier.** With two levels
  loaded, which `LevelDirector` do you get? Unambiguous from inside a behaviour,
  ambiguous from anywhere else. T0077's problem;
  [`08-frame-anatomy.md`](08-frame-anatomy.md) already flags the matching
  question about whether phases 4–9 run per-scene.
- **Self-registering statics inside a module are unproven here.** The intrusive
  list node should be safe — it is a POD with no destructor, and it is
  destruction that this toolchain punishes, not statics — but it has not been
  tried. Static-init order across TUs is also unspecified, so the walk should
  sort by name if anything ever depends on order.
- **`final` devirtualisation under `zig cc` is asserted, not measured.**
- **`hp::each<Base>` per-pool iteration is designed, never built.** Walking K
  contiguous pools with zero type checks should beat Unity's and Unreal's
  per-object `IsA` check over a heterogeneous list — but that is reasoning, not
  a benchmark.
- **A timer utility is implied and unowned.** `after(seconds, fn)` appears in
  the door above; Godot has `await get_tree().create_timer(3.0).timeout` and
  gameplay wants it constantly. Small, and the kind of thing that gets invented
  five times if nobody owns it — T0073's territory.
- **Vocabulary must stay coherent.** `process()` paired with `setProcess(bool)`,
  or `tick()` paired with `setTickEnabled(bool)`. Mixing them (`process()` +
  `stopTicking()`) is worse than either. The current choice is Godot's, on
  familiarity grounds.
- **`setProcess()` and `setEnabled()` are different ideas** and should not be
  one function. "Nothing to do right now" is transient and about performance;
  "this behaviour is off" is semantic. Godot conflates them; Unity has only the
  second, which is why it has no cheap way to stop ticking.
