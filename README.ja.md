# windows-dump-lab

**æ—¥æœ¬èªž** Â· [English](README.md) Â· [ç®€ä½“ä¸­æ–‡](README.zh-CN.md) Â· [í•œêµ­ì–´](README.ko.md)

[![hygiene](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml/badge.svg)](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078d4.svg)](#)
[![Cases](https://img.shields.io/badge/Cases-30-2ea44f.svg)](#)

<!-- translated-from: README.md sha256:b0f686ab2e13 -->

[windows-dump-analysis](../windows-dump-analysis) スキル向けのフォールトインジェクション実験場です。
30 個の選択可能な障害シナリオを持つ C++17 + Qt コンソールプログラム `CrashHarness.exe` をビルドし、
実行ごとに実際の `.dmp` を生成して、スキルのコマンドチェーンで解析できるようにします。

## スキルリポジトリとの関係

| リポジトリ | 内容 | 相手に依存するか |
|---|---|---|
| `windows-dump-analysis` | `SKILL.md` + `scripts/cdb-chain.ps1`（解析手順） | **しない** — 本実験場なしで動作する |
| `windows-dump-lab`（本リポジトリ） | 30 の障害ケース、CMake プロジェクト、期待値 | 「検証」段階のみ依存 |

意図した設計:

- **サブモジュールを使わない**。依存は一方向なので、スキルの利用者に C++ プロジェクトを取得させては
  いけません。本リポジトリは兄弟ディレクトリ `..\windows-dump-analysis` を探し、配置が違う場合は
  `-SkillDir` で指定します。
- **期待値は本リポジトリに置く**。対象のソースの隣に置き、`scripts\gen_expected_answers.ps1` が
  `// @fault` マーカーから再生成します。
- スキルは本リポジトリを回帰スイートとして参照し、本リポジトリは消費者として参照し返します。

## ビルドと実行

必要なもの: CMake ≥ 3.20、Ninja（または Visual Studio ジェネレーター）、MSVC x64、Qt 6（Qt 5 も可）、
検証段階には Windows SDK の Debugging Tools（`cdb.exe`）。

```powershell
powershell -NoProfile -File scripts\run_all_cases.ps1
```

初回は `harness/` を `-DCMAKE_PREFIX_PATH=<Qt>` で構成し、`/Od /Ob0 /Zi /GS` でビルドし、
`windeployqt` で Qt ランタイムを `output\bin` に配置してから各ケースを実行します。

```powershell
scripts\run_all_cases.ps1 -Cases 1,9,17      # サブセット
scripts\run_all_cases.ps1 -SkipBuild         # 既存の exe を再利用
scripts\run_all_cases.ps1 -QtDir C:\Qt\6.8.4\msvc2022_64
```

1 ケースの制限時間は既定 8 秒。リーク系（25–30）は 30 秒です。ハンドル情報付きの全メモリダンプを書くため
（ケース 25 だけで 200 MB を消費）8 秒には収まりません。

## ケース

| ソース | ケース | 内容 |
|---|---|---|
| `harness/cases/01_NullPtrCases.cpp` | 1–6 | null メンバー読み取り、null `this` 呼び出し、`qobject_cast(sender())`、終了時シングルトン、未初期化ポインタ、`GetProcAddress` の null |
| `harness/cases/02_UafAndMemoryCases.cpp` | 7–12 | vtable の dangling、二重解放、ヒープブロックヘッダー越え、`/GS` 越え、スタック枯渇、dangling 参照 |
| `harness/cases/03_QtFrameworkCases.cpp` | 13–18 | `deleteLater` + lambda、親子ツリー、別スレッドの Widget、シグナルループ、デストラクタ内の純粋仮想、`QModelIndex::internalPointer` |
| `harness/cases/04_ConcurrencyCases.cpp` | 19–24 | イテレータ無効化、`unordered_map` の競合、AB-BA デッドロック、`BlockingQueuedConnection` デッドロック、CPU スピン、無限待機 |
| `harness/cases/05_ResourceLeakCases.cpp` | 25–30 | 1 MB ヒープブロック、孤児 `QObject`、`QSharedPointer` の循環、GDI ハンドル、カーネルイベント、待機スレッド |

正確なダンプ名は `output\bin\CrashHarness.exe --list` で確認できます。

## ダンプの取得方法

`DumpHelper` は `SetUnhandledExceptionFilter`、`_set_purecall_handler`、
`_set_invalid_parameter_handler`、`std::set_terminate`、`SIGABRT` ハンドラー、
`SetErrorMode(SEM_NOGPFAULTERRORBOX)`、そして `0xC0000005`、`0xC0000374`、`0xC00000FD`、
`0xC0000409`、`0xC000001D`、`0xC0000094` に対する `AddVectoredExceptionHandler` を登録します。

ダンプは専用スレッド（1 MB スタック）が書き、プロセスの終了もそのスレッドが担当します。障害を起こした
スレッドは以降、メモリ確保も待機もしません — スタックオーバーフローでもダンプを残す唯一の方法です。
ハングとリークのケースは `StartWatchdog(name, delayMs, fullMemory)` を使い、遅延後にウォッチドッグが
ダンプ（リークは全メモリ + ハンドル情報）を書いて終了します。

隠さずに明記している差異:

| ケース | 実際の挙動 | 理由 |
|---|---|---|
| 09、20 | ブロックを専用ヒープに置き、破損ダンプは `MiniDumpNormal` | `0xC0000374` はヒープロック保持中に発生し、全メモリダンプはデッドロックする |
| 10 | 越え書きの後に `RaiseException(0xC0000409)` | 本物の `/GS` 失敗は `__fastfail` を通り VEH/SEH を迂回するため、ユーザーモードでは捕捉できない |
| 15 | 多くの場合クラッシュせずハングする | Qt 6.8 は別スレッドからの Widget 操作をデッドロックまで許容するため、ウォッチドッグで取得する |
| 19 | 走査中に erase し、移動してきた `nullptr` を通して呼ぶ | イテレータ無効化の「読み取り」は稀にしか落ちないため、この形が確実 |

## スキルによるダンプ検証

```powershell
powershell -NoProfile -File scripts\verify_with_skill.ps1
```

クラッシュダンプごとにスキルの `cdb-chain.ps1 -Chain sources` を実行し、報告された `file:line` を
`expected_answers.json` と照合（±3 行）して、ケース別の表と通過率を出します。ウォッチドッグのみの
ケース（15、21–30）は存在とサイズだけを確認します。

## 構成

```
harness/CMakeLists.txt       /Od /Ob0 /Zi /GS + windeployqt POST_BUILD
harness/main.cpp             --case N / --list、Qt ケースのみ QApplication を生成
harness/core/DumpHelper.*    静かな取得: VEH + CRT フック + ダンプスレッド + ウォッチドッグ
harness/cases/0[1-5]_*.cpp   30 のシナリオ
scripts/run_all_cases.ps1    ビルド、全ケース実行、ダンプ表の出力
scripts/gen_expected_answers.ps1  // @fault マーカーから期待値を再生成
scripts/verify_with_skill.ps1     スキルによるクロスチェック
expected_answers.json
output/                      gitignore: bin/（exe + pdb + Qt ランタイム）、dumps/
```

## ライセンス

[MIT](LICENSE)
