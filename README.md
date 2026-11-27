# windows-dump-lab

**English** · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

[![hygiene](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml/badge.svg)](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078d4.svg)](#)
[![Cases](https://img.shields.io/badge/Cases-30-2ea44f.svg)](#)

A fault-injection lab for the [windows-dump-analysis](../windows-dump-analysis) skill. It builds
`CrashHarness.exe` — a C++17 + Qt console program with 30 selectable failure scenarios — and turns
every run into a real `.dmp` that the skill's command chains can analyse.

## Relationship to the skill

| Repository | Contains | Depends on the other? |
|---|---|---|
| `windows-dump-analysis` | `SKILL.md` + `scripts/cdb-chain.ps1` (the analysis playbook) | **no** — it works without this lab |
| `windows-dump-lab` (this repo) | 30 fault cases, CMake build, expected answers | only for the optional verification step |

Deliberate choices:

- **No submodule.** The dependency is one-way, so the skill must not force its users to fetch a C++
  project; the lab instead locates a sibling checkout at `..\windows-dump-analysis` and accepts
  `-SkillDir` when the layout differs.
- **Expected answers live here**, next to the sources they describe, and are regenerated from the
  `// @fault` markers by `scripts\gen_expected_answers.ps1`.
- The skill links to this repo as its regression suite; this repo links back as its consumer.

## Build and run

Requirements: CMake ≥ 3.20, Ninja (or a Visual Studio generator), MSVC x64, Qt 6 (Qt 5 is accepted
as a fallback), and the Windows SDK Debugging Tools (`cdb.exe`) for the verification step.

```powershell
powershell -NoProfile -File scripts\run_all_cases.ps1
```

The first run configures `harness/` with `-DCMAKE_PREFIX_PATH=<Qt>`, builds with `/Od /Ob0 /Zi /GS`,
deploys the Qt runtime into `output\bin` via `windeployqt`, and then runs each case:

```powershell
scripts\run_all_cases.ps1 -Cases 1,9,17      # subset
scripts\run_all_cases.ps1 -SkipBuild         # reuse output\bin\CrashHarness.exe
scripts\run_all_cases.ps1 -QtDir C:\Qt\6.8.4\msvc2022_64
```

Every case is allowed 8 s; the leak cases (25–30) get 30 s because they write full-memory dumps with
handle data (case 25 alone commits 200 MB).

## Cases

| Source file | Cases | Focus |
|---|---|---|
| `harness/cases/01_NullPtrCases.cpp` | 1–6 | null member read, null `this` call, `qobject_cast(sender())`, shutdown singleton, uninitialised wild pointer, `GetProcAddress` null |
| `harness/cases/02_UafAndMemoryCases.cpp` | 7–12 | dangling vtable, double free, heap block-header overrun, `/GS` overrun, stack exhaustion, dangling reference |
| `harness/cases/03_QtFrameworkCases.cpp` | 13–18 | `deleteLater` + lambda, parent/child tree, cross-thread widget, signal loop, purecall in destructor, `QModelIndex::internalPointer` |
| `harness/cases/04_ConcurrencyCases.cpp` | 19–24 | iterator invalidation, `unordered_map` race, AB-BA deadlock, `BlockingQueuedConnection` deadlock, CPU spin, infinite wait |
| `harness/cases/05_ResourceLeakCases.cpp` | 25–30 | 1 MB heap blocks, orphan `QObject`s, `QSharedPointer` cycle, GDI handles, kernel events, parked threads |

Run `output\bin\CrashHarness.exe --list` for the exact dump names.

## How the dumps are captured

`DumpHelper` installs `SetUnhandledExceptionFilter`, `_set_purecall_handler`,
`_set_invalid_parameter_handler`, `std::set_terminate`, a `SIGABRT` handler,
`SetErrorMode(SEM_NOGPFAULTERRORBOX)` and an `AddVectoredExceptionHandler` first-chance filter for
`0xC0000005`, `0xC0000374`, `0xC00000FD`, `0xC0000409`, `0xC000001D`, `0xC0000094`.

The dump itself is written by a dedicated thread (1 MB stack) that also owns process termination, so
the faulting thread never allocates or waits after the fault — the only way a stack overflow can still
produce a dump. Hang and leak cases arm `StartWatchdog(name, delayMs, fullMemory)`: after the delay a
watchdog thread writes the dump (full memory + handle data for leaks) and terminates.

Deviations that the lab documents instead of hiding:

| Case | Behaviour | Why |
|---|---|---|
| 09, 20 | blocks live on a private heap, corruption dump uses `MiniDumpNormal` | `0xC0000374` is raised while the heap lock is held; a full-memory dump of the process heap deadlocks |
| 10 | overrun followed by `RaiseException(0xC0000409)` | a genuine `/GS` failure reaches `__fastfail`, which bypasses VEH/SEH, so user mode cannot capture it |
| 15 | usually hangs instead of crashing | Qt 6.8 tolerates cross-thread widget calls until they deadlock; the watchdog captures that instead |
| 19 | erases while iterating, then calls through the shifted-in `nullptr` | iterator-invalidation reads rarely fault; this form is deterministic |

## Verifying the dumps with the skill

```powershell
powershell -NoProfile -File scripts\verify_with_skill.ps1
```

This runs the skill's `cdb-chain.ps1 -Chain sources` against every crash dump, compares the reported
`file:line` with `expected_answers.json` (±3 lines), and reports a per-case table plus a pass rate.
Cases that only have watchdog dumps (15, 21–30) are checked for existence and non-zero size instead.

## Layout

```
harness/CMakeLists.txt       /Od /Ob0 /Zi /GS + windeployqt POST_BUILD
harness/main.cpp             --case N / --list, creates QApplication only for Qt cases
harness/core/DumpHelper.*    silent capture: VEH + CRT hooks + dump thread + watchdog
harness/cases/0[1-5]_*.cpp   the 30 scenarios
scripts/run_all_cases.ps1    build, run every case, print the dump table
scripts/gen_expected_answers.ps1  rebuild expected_answers.json from // @fault markers
scripts/verify_with_skill.ps1     cross-check dumps against the skill
expected_answers.json
output/                      gitignored: bin/ (exe + pdb + Qt runtime), dumps/
```

## License

[MIT](LICENSE)
