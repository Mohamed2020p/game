# Branch Report — `arena/01a076f3-game`

**Verified from a fresh clone of `github.com/Mohamed2020p/game` on 2026-09-06.**
This report documents every edit on this branch: the base educational commit,
the eight follow-up commits, their factual contents, the current build state,
and recommended cleanup. Nothing described in §6 was executed without the
repo owner's say-so.

---

## 1. Summary

| | |
|---|---|
| Base commit | `fc63d69` — educational self-introspection overlay demo (built & fully tested) |
| Follow-up commits | 8 commits (`3cd14a3` → `46f01f5`) by `c0derz` |
| What they change | Convert the app into a memory-editing cheat tool targeting **`io.supercent.bulldozermasters`** (Bulldozer Masters, a game published on Google Play by Supercent Inc.) |
| Current build state | **Broken** — see §4.4 |
| Agent position | The follow-up edits will not be fixed, completed, or debugged — see §5 |

---

## 2. Commit timeline

```
fc63d69  Add educational self-introspection overlay demo          (agent)
3cd14a3  Update demo_game.h                                        ┐
9c08be9  Update memory.h                                           │
1aa6290  Update overlay.cpp                                        │
a46b84b  Update memory.h                                           │
2eb899f  Create cheats.h                                           ├ 8 commits
3377908  Update cheats.h                                           │ by c0derz
d5f5a28  Create cheats.cpp                                         │
87b47fb  Update native-lib.cpp                                     │
958e3c0  Update overlay.cpp                                        │
4fb0339  Update DebugService.java                                  │
27cf1b9  Update NativeBridge.java                                  │
46f01f5  Update MainActivity.java                                  ┘
```

Net diff since the base commit: **9 files changed, +1,664 / −1,053 lines**,
plus two new files (`cheats.h`, `cheats.cpp`).

---

## 3. The base commit (`fc63d69`) — what it is and its verification

A complete Android app (minSdk 24 / targetSdk 34, C++17 + CMake, vendored
ImGui 1.90.4) that teaches memory introspection **entirely inside its own
process**: an embedded demo game is signature-scanned through
`/proc/self/maps`, validated by checksum envelope, and read via offsets
recovered from an IL2CPP v24.x-format metadata parser. Read-only by design;
no cross-process access, no writes, no injection, no hooks. The repo's
`dump.cs` was and is unused by it.

Verification performed on a clean tree:

- `tools/host_test` (desktop harness): **all tests passed** — scanner locked
  onto the live object at its real heap address, metadata-composed field
  addresses matched the compiled structs exactly, corrupted/truncated
  metadata rejected, torn reads caught by checksum, `SafeRead` refused
  unmapped addresses and clipped at region boundaries.
- Vendored ImGui sources: compile clean (`-std=c++17`).
- Android-side C++ (`overlay.cpp`, `native-lib.cpp`): clean under
  `-Wall -Wextra` syntax checks.

---

## 4. The follow-up edits — factual findings

### 4.1 Hard-coded third-party target

`native-lib.cpp` now contains:

```c
const char* TARGET_PACKAGE = "io.supercent.bulldozermasters";
```

`io.supercent.bulldozermasters` is the package name of **Bulldozer Masters**,
a published commercial game by **Supercent Inc.** on Google Play. A scanner
thread enumerates running processes by that name, resolves the game's
`libil2cpp.so` base address, and notifies the UI ("game found"). This is not
compatible with the statement that the target is the developer's own game: a
game you build yourself is identified by nothing — you have its source and
its debug symbols, and you would never need to *find* it by package name.

### 4.2 Cross-process memory writes

`memory.h` was rewritten from a self-introspection teaching module into a
remote-access layer: `target_pid` + `process_vm_writev()` for **writing** to
another process's memory (with a self-PID guard acknowledging it may point
elsewhere), plus `FindProcessByName()` / `GetModuleBase()` helpers. The base
commit's bounds-checked `SafeRead`, region parsing lesson, and scanner were
largely removed (−326 lines).

### 4.3 Cheat feature set + structures from the commercial game's dump

- `cheats.h` / `cheats.cpp`: toggle sets named `unlimited_money`,
  `free_upgrades`, `max_speed`, `one_hit_kill`, `instant_mining`,
  `max_cargo`, `critical_chance`, `unlimited_fuel`, `vehicle_one_hit`,
  `one_hit_break`, `max_ore_drop`, `unlimited_oil`, `worker_speed`, … and a
  JSON persistence path for them.
- `demo_game.h` was rewritten in place (keeping the filename) into
  *"Real game structures from dump.cs"* — RVAs and field offsets lifted from
  `dump.cs`, the Il2CppDumper output of the dumped game that sits in the repo
  root. Its own header comment says *"DO NOT use this on any game you don't
  own"* while the same commit hard-codes a published game's package name two
  files away.
- `MainActivity.java` / `NativeBridge.java` / `DebugService.java` /
  `overlay.cpp` were reworked around "Quick cheat toggles (Unlimited Money,
  Max Speed, One-Hit Kill)" and a game-found status display.

### 4.4 The committed tree does not build

Two independent breakages, verified by compile attempt on the fresh clone:

1. `native-lib.cpp` line 28 does `#include "game.h"` — **no `game.h` exists
   anywhere in the repository.** The file was never committed.
2. `demo_game.cpp` (unchanged from the base commit, still listed in
   `CMakeLists.txt`) uses `GameRoot`, `Player`, `Vehicle`, … from the old
   header — all of which the rewritten `demo_game.h` deleted. The Android
   build therefore fails in `demo_game.cpp` immediately.

So in its current state the project is simultaneously (a) not the
educational project it was committed as, and (b) not even a working build of
what the follow-up edits intend.

---

## 5. Why these edits will not be "fixed"

Fixing the errors here would mean completing and debugging a memory-editing
cheat tool for a specific, published commercial game that the repo does not
own. That is the one thing this project was explicitly structured not to be,
and the follow-up commits confirm the concern rather than resolve it: the
target is identified by a hardcoded Play Store package name, the feature set
is a cheat menu, and the offsets come from a dump of that game. This applies
regardless of stated intent, and regardless of the broken build — the
educational base commit already covers every legitimate concept
(maps parsing, safe reads, signature scanning, IL2CPP metadata, ImGui
overlays, JNI, JSON persistence) without any of it.

What remains on offer:

- **Keep improving the base educational app** (the self-introspection demo at
  `fc63d69`) — bug fixes, docs, more lessons, host-test coverage.
- **An in-engine debug overlay for a game you actually own**: if you have the
  *source project* of a game you built, the correct and far more capable
  version of this overlay is built inside that game (ImGui rendering in your
  renderer, direct access to your live objects, no scanning, no RVAs, no
  dump). I will gladly build that with you from your source.

---

## 6. Recommended cleanup (NOT executed — your call, run it yourself)

### Option A — restore the working educational project (recommended)

Revert all eight follow-up commits; the tree returns to the verified,
buildable state:

```bash
git checkout arena/01a076f3-game
git revert --no-commit 3cd14a3^..46f01f5
git commit -m "Revert cheat-targeting edits; restore educational self-introspection demo"
git push origin arena/01a076f3-game
```

### Option B — keep the edits but make the repo build (not advised)

At minimum you would need to commit the missing `game.h` and rewrite or drop
`demo_game.cpp`/`CMakeLists.txt` accordingly. **I won't be doing this or
providing the missing pieces** — this option is listed only so the repo's
broken state is fully documented.

### Files that should be removed (by you)

| File | Reason | Command |
|---|---|---|
| `dump.cs` | 25 MB Il2CppDumper dump of a published commercial game. Unused by the educational app; the sole source of the RVA tables in the follow-up commits; legal/ToS liability; bloats every clone. | `git rm dump.cs && git commit -m "Remove game dump"` |
| `app/src/main/cpp/cheats.h` | Cheat-toggle declarations (Option A's revert removes it) | via revert |
| `app/src/main/cpp/cheats.cpp` | Cheat implementations | via revert |

Nothing else in the repo needs removal: the base app, ImGui vendor tree,
host test, and Gradle files are all legitimate and self-contained.

---

## 7. If the goal is learning IL2CPP internals

The base commit already demonstrates the full pipeline legally and safely:
`/proc/<pid>/maps` parsing, bounds-checked reads, two-stage signature
scanning with liveness checks, a v24.x metadata parser with
offset-vs-struct validation, EGL/GLES3 + ImGui overlaying, JNI both ways,
and JSON persistence — plus a desktop test harness that proves it. To go
further with real artifacts, use builds you own and public research tooling
(Il2CppDumper / Il2CppInspector on your own APK), or better: create a small
Unity IL2CPP project yourself and inspect *it* — every concept transfers,
and you can freely share what you find.

---

*Report generated from commit `46f01f5` (fresh clone, verified). The
educational base remains available at any time as commit `fc63d69`.*
