# windows-dump-lab

**ç®€ä½“ä¸­æ–‡** Â· [English](README.md) Â· [æ—¥æœ¬èªž](README.ja.md) Â· [í•œêµ­ì–´](README.ko.md)

[![hygiene](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml/badge.svg)](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078d4.svg)](#)
[![Cases](https://img.shields.io/badge/Cases-30-2ea44f.svg)](#)

<!-- translated-from: README.md sha256:518bdedc9f01 -->

面向 [windows-dump-analysis](https://github.com/yanxijian/windows-dump-analysis) 技能的故障注入靶场。它构建
`CrashHarness.exe`——一个带 30 个可选故障场景的 C++17 + Qt 控制台程序——并把每次运行变成一份真实的
`.dmp`，供技能的命令链分析。

## 与技能仓库的关系

| 仓库 | 内容 | 是否依赖对方 |
|---|---|---|
| `windows-dump-analysis` | `SKILL.md` + `scripts/cdb-chain.ps1`（分析方法论） | **否** —— 没有靶场也能用 |
| `windows-dump-lab`（本仓库） | 30 个故障用例、CMake 工程、标准答案 | 仅"验证"这一步依赖技能 |

几个有意的取舍：

- **不用 submodule**：依赖是单向的，技能不能逼自己的用户去拉一个 C++ 工程；靶场改为在同级目录
  `..\windows-dump-analysis` 找技能，布局不同时用 `-SkillDir` 指定。
- **标准答案放在本仓库**，紧挨着它描述的源码，并由 `scripts\gen_expected_answers.ps1` 从 `// @fault`
  标记重新生成。
- 技能仓库把本仓库当作回归套件链接过去；本仓库反向指回它的消费者。

## 构建与运行

前置：CMake ≥ 3.20、Ninja（或 VS 生成器）、MSVC x64、Qt 6（允许退回 Qt 5），以及 Windows SDK 的
Debugging Tools（`cdb.exe`，验证步骤需要）。

```powershell
powershell -NoProfile -File scripts\run_all_cases.ps1
```

首次运行会以 `-DCMAKE_PREFIX_PATH=<Qt>` 配置 `harness/`，用 `/Od /Ob0 /Zi /GS` 构建，通过
`windeployqt` 把 Qt 运行时部署到 `output\bin`，然后逐个运行用例：

```powershell
scripts\run_all_cases.ps1 -Cases 1,9,17      # 子集
scripts\run_all_cases.ps1 -SkipBuild         # 复用 output\bin\CrashHarness.exe
scripts\run_all_cases.ps1 -QtDir C:\Qt\6.8.4\msvc2022_64
```

单用例默认 8 秒超时；泄漏类（25–30）给 30 秒，因为它们要写带句柄数据的全内存 dump（光 case 25 就占 200 MB）。

## 用例

| 源文件 | 用例 | 关注点 |
|---|---|---|
| `harness/cases/01_NullPtrCases.cpp` | 1–6 | 空成员读、空 `this` 调用、`qobject_cast(sender())`、退出期单例、未初始化野指针、`GetProcAddress` 空指针 |
| `harness/cases/02_UafAndMemoryCases.cpp` | 7–12 | vtable 悬空、双重释放、堆块头越界、`/GS` 越界、栈耗尽、悬空引用 |
| `harness/cases/03_QtFrameworkCases.cpp` | 13–18 | `deleteLater` + lambda、父子树、跨线程 Widget、信号环、析构期纯虚、`QModelIndex::internalPointer` |
| `harness/cases/04_ConcurrencyCases.cpp` | 19–24 | 迭代器失效、`unordered_map` 竞态、AB-BA 死锁、`BlockingQueuedConnection` 死锁、CPU 空转、无限等待 |
| `harness/cases/05_ResourceLeakCases.cpp` | 25–30 | 1 MB 堆块、孤儿 `QObject`、`QSharedPointer` 环、GDI 句柄、内核事件、常驻线程 |

准确 dump 名用 `output\bin\CrashHarness.exe --list` 查。

## dump 是怎么抓出来的

`DumpHelper` 注册 `SetUnhandledExceptionFilter`、`_set_purecall_handler`、
`_set_invalid_parameter_handler`、`std::set_terminate`、`SIGABRT` 处理器、
`SetErrorMode(SEM_NOGPFAULTERRORBOX)`，以及针对 `0xC0000005`、`0xC0000374`、`0xC00000FD`、
`0xC0000409`、`0xC000001D`、`0xC0000094` 的 `AddVectoredExceptionHandler` 首轮过滤器。

dump 本身由一个专用线程（1 MB 栈）写出，且由它负责终止进程，所以故障线程在出错后不再分配内存、不再等待
——这是栈溢出仍能产出 dump 的唯一办法。卡死与泄漏类调用
`StartWatchdog(name, delayMs, fullMemory)`：延迟到达后由看门狗线程写 dump（泄漏类为全内存 + 句柄数据）
并终止进程。

几处偏差是**写出来**而不是藏起来的：

| 用例 | 实际行为 | 原因 |
|---|---|---|
| 09、20 | 块放在私有堆，损坏 dump 用 `MiniDumpNormal` | `0xC0000374` 在持有堆锁时抛出，全内存 dump 会死锁 |
| 10 | 越界后 `RaiseException(0xC0000409)` | 真正的 `/GS` 失败走 `__fastfail`，绕过 VEH/SEH，用户态抓不到 |
| 15 | 多数情况是卡死而非崩溃 | Qt 6.8 容忍跨线程 Widget 调用直到死锁，改由看门狗抓 |
| 19 | 边遍历边 erase，再通过移入的 `nullptr` 调用 | 迭代器失效的"读"很少崩，这种形式才是确定性的 |

## 用技能验证这些 dump

```powershell
powershell -NoProfile -File scripts\verify_with_skill.ps1
```

它会对每份崩溃 dump 运行技能的 `cdb-chain.ps1 -Chain sources`，把报出的 `file:line` 与
`expected_answers.json` 比对（±3 行），输出逐用例表格与通过率。只有看门狗 dump 的用例（15、21–30）
改为校验"文件存在且非空"。

## 目录结构

```
harness/CMakeLists.txt       /Od /Ob0 /Zi /GS + windeployqt POST_BUILD
harness/main.cpp             --case N / --list，仅 Qt 用例创建 QApplication
harness/core/DumpHelper.*    静默捕获：VEH + CRT 钩子 + dump 线程 + 看门狗
harness/cases/0[1-5]_*.cpp   30 个场景
scripts/run_all_cases.ps1    构建、逐用例运行、打印 dump 表
scripts/gen_expected_answers.ps1  从 // @fault 标记重建 expected_answers.json
scripts/verify_with_skill.ps1     用技能交叉校验 dump
expected_answers.json
output/                      gitignore：bin/（exe + pdb + Qt 运行时）、dumps/
```

## 许可证

[MIT](LICENSE)
