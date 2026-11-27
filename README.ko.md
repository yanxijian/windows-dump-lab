# windows-dump-lab

**í•œêµ­ì–´** Â· [English](README.md) Â· [ç®€ä½“ä¸­æ–‡](README.zh-CN.md) Â· [æ—¥æœ¬èªž](README.ja.md)

[![hygiene](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml/badge.svg)](https://github.com/yanxijian/windows-dump-lab/actions/workflows/hygiene.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078d4.svg)](#)
[![Cases](https://img.shields.io/badge/Cases-30-2ea44f.svg)](#)

<!-- translated-from: README.md sha256:518bdedc9f01 -->

[windows-dump-analysis](https://github.com/yanxijian/windows-dump-analysis) 스킬을 위한 오류 주입 실험장입니다. 선택 가능한 30개
장애 시나리오를 가진 C++17 + Qt 콘솔 프로그램 `CrashHarness.exe` 를 빌드하고, 실행할 때마다 실제 `.dmp`
를 만들어 스킬의 명령 체인으로 분석할 수 있게 합니다.

## 스킬 저장소와의 관계

| 저장소 | 내용 | 상대에 의존하는가 |
|---|---|---|
| `windows-dump-analysis` | `SKILL.md` + `scripts/cdb-chain.ps1` (분석 절차) | **아니오** — 이 실험장 없이 동작 |
| `windows-dump-lab` (이 저장소) | 30개 장애 케이스, CMake 프로젝트, 기대값 | "검증" 단계에서만 의존 |

의도한 설계:

- **서브모듈을 쓰지 않습니다.** 의존은 단방향이므로 스킬 사용자에게 C++ 프로젝트를 받게 강요해서는 안 됩니다.
  이 저장소는 형제 디렉터리 `..\windows-dump-analysis` 를 찾고, 배치가 다르면 `-SkillDir` 로 지정합니다.
- **기대값은 이 저장소에 둡니다.** 대상 소스 옆에 두고 `scripts\gen_expected_answers.ps1` 이
  `// @fault` 표시에서 다시 생성합니다.
- 스킬은 이 저장소를 회귀 스위트로 참조하고, 이 저장소는 소비자로서 다시 참조합니다.

## 빌드와 실행

필요 환경: CMake ≥ 3.20, Ninja(또는 Visual Studio 생성기), MSVC x64, Qt 6(Qt 5 허용), 검증 단계에는
Windows SDK Debugging Tools(`cdb.exe`).

```powershell
powershell -NoProfile -File scripts\run_all_cases.ps1
```

첫 실행은 `harness/` 를 `-DCMAKE_PREFIX_PATH=<Qt>` 로 구성하고 `/Od /Ob0 /Zi /GS` 로 빌드하며,
`windeployqt` 로 Qt 런타임을 `output\bin` 에 배치한 뒤 각 케이스를 실행합니다.

```powershell
scripts\run_all_cases.ps1 -Cases 1,9,17      # 일부만
scripts\run_all_cases.ps1 -SkipBuild         # 기존 exe 재사용
scripts\run_all_cases.ps1 -QtDir C:\Qt\6.8.4\msvc2022_64
```

케이스당 기본 제한 시간은 8초입니다. 누수 계열(25–30)은 30초인데, 핸들 정보를 포함한 전체 메모리 덤프를
써야 하기 때문입니다(케이스 25만 200 MB).

## 케이스

| 소스 | 케이스 | 내용 |
|---|---|---|
| `harness/cases/01_NullPtrCases.cpp` | 1–6 | null 멤버 읽기, null `this` 호출, `qobject_cast(sender())`, 종료 시 싱글턴, 미초기화 포인터, `GetProcAddress` null |
| `harness/cases/02_UafAndMemoryCases.cpp` | 7–12 | vtable dangling, 이중 해제, 힙 블록 헤더 넘어 쓰기, `/GS` 넘어 쓰기, 스택 고갈, dangling 참조 |
| `harness/cases/03_QtFrameworkCases.cpp` | 13–18 | `deleteLater` + lambda, 부모-자식 트리, 다른 스레드의 Widget, 시그널 루프, 소멸자 내 순수 가상, `QModelIndex::internalPointer` |
| `harness/cases/04_ConcurrencyCases.cpp` | 19–24 | 반복자 무효화, `unordered_map` 경쟁, AB-BA 교착, `BlockingQueuedConnection` 교착, CPU 스핀, 무한 대기 |
| `harness/cases/05_ResourceLeakCases.cpp` | 25–30 | 1 MB 힙 블록, 고아 `QObject`, `QSharedPointer` 순환, GDI 핸들, 커널 이벤트, 대기 스레드 |

정확한 덤프 이름은 `output\bin\CrashHarness.exe --list` 로 확인합니다.

## 덤프를 얻는 방식

`DumpHelper` 는 `SetUnhandledExceptionFilter`, `_set_purecall_handler`,
`_set_invalid_parameter_handler`, `std::set_terminate`, `SIGABRT` 핸들러,
`SetErrorMode(SEM_NOGPFAULTERRORBOX)`, 그리고 `0xC0000005`, `0xC0000374`, `0xC00000FD`,
`0xC0000409`, `0xC000001D`, `0xC0000094` 에 대한 `AddVectoredExceptionHandler` 를 등록합니다.

덤프는 전용 스레드(1 MB 스택)가 쓰고 프로세스 종료도 그 스레드가 담당합니다. 장애를 낸 스레드는 이후 메모리를
할당하지도, 대기하지도 않습니다 — 스택 오버플로에서도 덤프를 남기는 유일한 방법입니다. 멈춤과 누수 케이스는
`StartWatchdog(name, delayMs, fullMemory)` 를 쓰며, 지연 후 워치독 스레드가 덤프(누수는 전체 메모리 + 핸들
정보)를 쓰고 종료합니다.

숨기지 않고 명시한 차이:

| 케이스 | 실제 동작 | 이유 |
|---|---|---|
| 09, 20 | 블록을 전용 힙에 두고, 손상 덤프는 `MiniDumpNormal` | `0xC0000374` 는 힙 잠금을 보유한 채 발생해 전체 메모리 덤프가 교착됨 |
| 10 | 넘어 쓰기 후 `RaiseException(0xC0000409)` | 실제 `/GS` 실패는 `__fastfail` 로 VEH/SEH 를 우회해 사용자 모드에서 잡을 수 없음 |
| 15 | 대개 크래시하지 않고 멈춤 | Qt 6.8 은 다른 스레드의 Widget 조작을 교착까지 허용하므로 워치독으로 수집 |
| 19 | 순회 중 erase 후 이동해 온 `nullptr` 로 호출 | 반복자 무효화의 "읽기"는 좀처럼 죽지 않아 이 형태가 확실함 |

## 스킬로 덤프 검증

```powershell
powershell -NoProfile -File scripts\verify_with_skill.ps1
```

각 크래시 덤프에 대해 스킬의 `cdb-chain.ps1 -Chain sources` 를 실행하고 보고된 `file:line` 을
`expected_answers.json` 과 비교(±3줄)하여 케이스별 표와 통과율을 출력합니다. 워치독 덤프만 있는
케이스(15, 21–30)는 존재와 크기만 확인합니다.

## 구성

```
harness/CMakeLists.txt       /Od /Ob0 /Zi /GS + windeployqt POST_BUILD
harness/main.cpp             --case N / --list, Qt 케이스만 QApplication 생성
harness/core/DumpHelper.*    조용한 수집: VEH + CRT 훅 + 덤프 스레드 + 워치독
harness/cases/0[1-5]_*.cpp   30개 시나리오
scripts/run_all_cases.ps1    빌드, 전체 케이스 실행, 덤프 표 출력
scripts/gen_expected_answers.ps1  // @fault 표시에서 기대값 재생성
scripts/verify_with_skill.ps1     스킬로 교차 검증
expected_answers.json
output/                      gitignore: bin/(exe + pdb + Qt 런타임), dumps/
```

## 라이선스

[MIT](LICENSE)
