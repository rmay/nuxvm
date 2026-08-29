# Important
Never add new opcodes.

Cloister is a single-app fantasy machine (Varvara/uxn-shaped): one program owns the screen. I/O is Plan 9 VFS files, not Varvara MMIO device ports. Apps look like Macintosh System 6. Do not add a window manager or multi-app rio.

# Versioning
We use the Kelvin versioning system (spec: https://jtobin.io/kelvin-versioning). Anything hotter can run something just as hot or colder as itself, but no guarantees something colder will run on hotter. Nux opcodes and implementation is 300K, everything else is 400K unless otherwise specified.

## Kelvin versioning spec

**Motivation.** Standards tend to fail in one of two ways: they freeze incompatibly forever (ASCII, IPv4), or they stay open to extension indefinitely and become an unbounded moving target. Kelvin versioning fixes this by counting *down*: a version is a "temperature" that only ever decreases, until it hits absolute zero (0K), after which the component is permanently frozen and MUST NOT change again. Higher numbers are hotter/earlier; a component gets colder (more final) with every release, never hotter.

**Core rules**, for any component A following kelvin versioning:

1. **Version format**: A's version SHALL be a nonnegative integer.
2. **Immutability**: A, at any specific version, MUST NOT be modified after release.
3. **Absolute zero**: once A is at version 0, new versions of A MUST NOT be released — 0 is permanently frozen.
4. **Monotonic decrease**: every new release of A MUST be assigned a new version, and it MUST be strictly less than the previous one.
5. **Dependency constraints**: when component A supports (depends on) component B, also kelvin-versioned: either both A and B MUST be at version 0, or B's version MUST be strictly greater than A's version — a supporting component must always run colder/more-final than what it supports. If a new version of A is released and that version still supports B, a new version of B MUST also be released. These constraints apply recursively through the whole dependency chain, so a component's version implicitly pins the versions of everything under it.

**Worked example** (illustrative, not part of this project's own numbers): a stack A(10K) → B(20K) → {C(21K), D(30K)}, where "→" means "is depended on by."
- A patch to D can just release D at 29K on its own (rule 4).
- A breaking change to A forces a cascade: A releases at 9K, which forces B to 19K, which forces C to 20K and D to 28K (rule 5, recursively).
- Now a harmless improvement to C is *blocked*: cooling C from 20K can reach at best 19K, which equals B's current version — not allowed except when both are at 0K (rule 5's strict inequality). This is the "telescoping" effect: a component can get stuck until its parent cools further.
- Once B next releases at 18K (forcing C to 19K, D to 27K), that gap reopens and the previously-blocked C improvement can ship.
- A component can also be replaced outright: C and D can be deprecated and replaced by a new component E, which starts at any temperature colder than its parent (e.g. 40K under a 18K B) — the starting number is otherwise unconstrained, and third parties can build on E independently of the rest of the stack.

**Collective kelvin versioning** — versioning a whole stack of components together under one number (as Urbit does for its kernel) — adds a primary-index component plus a fractional "temperature" in `[0, 1)`, format `[primary_index_version].[fractional_temp]K`:

1. The fractional temperature SHALL be a real number in `[0, 1)`.
2. The fractional temperature MAY be `0` only when the primary index itself is at version 0.
3. A released collective version MUST NOT be modified afterward.
4. At primary-index 0 with fractional temperature 0, new collective versions MUST NOT be released (the whole-stack equivalent of rule 3).
5. Every new collective release MUST be strictly less than the previous one.
6. When a subordinate component changes but the primary index doesn't, the fractional temperature MUST decrease.
7. Fractional temperatures SHOULD decrease along `.9, .8, .7, .., .1, .01, .001, .0001, ..`.
8. When the primary index itself cools, the fractional temperature SHOULD reset to `.9`.
9. A stack MAY be "reindexed" to a different primary component, provided the new primary is either colder than the old one or reaches version 0 together with it.

**Caveat (release candidates).** Starting a component too close to 0K risks running out of room to iterate before freezing. The spec's answer: release candidates are unlimited at any given temperature, suffixed `.rc1`, `.rc2`, etc., and should be used liberally while iterating before committing to an actual (numbered) cooldown.