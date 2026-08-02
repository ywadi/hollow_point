# T0070 — Networking

| | |
|---|---|
| **Status** | 🔜 TODO |
| **Priority** | Low |
| **Complexity** | Very Complex |
| **Phase** | 13 — Networking |
| **Created** | 2026-08-03 |

> **Placeholder epic**, opened to be sorted out later. Recorded now because
> networking is the one subsystem that, if ever wanted, imposes constraints on
> systems built long before it.

## Why

Not currently required. But multiplayer is unusual among subsystems: it cannot be
cleanly bolted on, because it constrains **determinism, state ownership and the
update loop** — all of which are decided in Phases 2-4.

The cheap thing to do now is decide whether it is *ever* plausible. Ruling it out
is free; leaving it ambiguous means either over-engineering for it or being unable
to add it.

## The constraints it would impose

- **Determinism** — lockstep models need bit-identical simulation across machines,
  which constrains float usage, threading order and physics configuration (T0051).
  Jolt is deterministic *if configured for it*, and that is a decision made at
  integration, not afterwards.
- **State authority** — who owns an entity's transform: the server, or the client
  predicting it? This lands directly on T0021 and T0062, since behaviours would
  need to know whether they run authoritatively.
- **Fixed timestep** — already required by physics (T0057), and doubly so here.
- **Serialization** — network state is a third serialization path alongside YAML
  and the binary cache (T0020), with very different constraints: small, fast,
  delta-compressed, versioned across builds.

## Rough scope

- [ ] Decide whether multiplayer is plausible at all — **needs the owner**
- [ ] If yes: choose a model (lockstep, client-server with prediction, rollback)
- [ ] Choose a transport (ENet, GameNetworkingSockets, custom UDP)
- [ ] Entity replication and ownership
- [ ] Client prediction and reconciliation
- [ ] Snapshot/delta serialization, separate from asset serialization

## Notes / findings

**The honest position: doing nothing now is correct, provided the decision is
recorded rather than drifting.** Building networking abstractions speculatively is
a well-known way to add complexity that never pays off.

The single cheapest hedge, if multiplayer is even remotely possible: keep gameplay
state **in components** rather than in behaviour instances (which T0062 and T0048
already require for hot reload). Replicating component data is tractable;
replicating arbitrary object state is not. That alignment is convenient — the
hot-reload discipline and the networking-friendly discipline are the same one.

If multiplayer is definitively ruled out, say so in the decision log so nobody
designs around a possibility that no longer exists.
