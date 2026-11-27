#Requires -Version 5.1
<#
.SYNOPSIS
    Regenerate expected_answers.json from the // @fault markers in the case sources.

.DESCRIPTION
    Every case marks its crashing statement (or leak source) with a "// @fault"
    comment. This script maps those markers to case ids, collects the optional
    root-cause keywords, and rewrites expected_answers.json with repo-relative
    paths, so the standard answers can never drift from the sources.

    Marker order does not always follow the case order (case 16 lives at file
    scope for moc), so each file declares its order explicitly.

.EXAMPLE
    powershell -NoProfile -File scripts\gen_expected_answers.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

# Per-file case ids in source order. Keep in sync when a case moves.
$files = @(
    @{
        path  = 'harness/cases/01_NullPtrCases.cpp'
        order = @(1, 2, 3, 4, 5, 6)
    }
    @{
        path  = 'harness/cases/02_UafAndMemoryCases.cpp'
        order = @(7, 8, 9, 10, 11, 12)
    }
    @{
        path  = 'harness/cases/03_QtFrameworkCases.cpp'
        order = @(13, 14, 15, 17, 18, 16)   # case 16 is declared last, at file scope
    }
    @{
        path  = 'harness/cases/04_ConcurrencyCases.cpp'
        order = @(19, 20, 21, 22, 23, 24)
    }
    @{
        path  = 'harness/cases/05_ResourceLeakCases.cpp'
        order = @(25, 26, 27, 28, 29, 30)
    }
)

$names = @{
    1 = '01_nullptr_member_read';            2 = '02_nullptr_virtual_call'
    3 = '03_qt_qobject_cast_null';           4 = '04_singleton_shutdown_null'
    5 = '05_uninitialized_wild_ptr';         6 = '06_dll_getprocaddress_null'
    7 = '07_cpp_direct_uaf_vtable';          8 = '08_double_free_heap'
    9 = '09_heap_buffer_overflow';           10 = '10_stack_buffer_overflow'
    11 = '11_stack_overflow_recursion';      12 = '12_dangling_reference_return'
    13 = '13_qt_deleteLater_lambda_uaf';     14 = '14_qt_parent_tree_uaf'
    15 = '15_qt_cross_thread_ui';            16 = '16_qt_signal_loop_overflow'
    17 = '17_qt_slot_during_destruction';    18 = '18_qt_invalid_model_index'
    19 = '19_stl_iterator_invalidation';     20 = '20_multithread_map_race'
    21 = '21_deadlock_abba_mutex';           22 = '22_deadlock_sendmessage'
    23 = '23_hang_main_thread_cpu';          24 = '24_hang_main_thread_io'
    25 = '25_leak_cpp_heap_blocks';          26 = '26_leak_qt_orphan_qobjects'
    27 = '27_leak_qsharedpointer_cycle';     28 = '28_leak_gdi_handles'
    29 = '29_leak_kernel_event_handles';     30 = '30_leak_zombie_threads'
}

$keywords = @{
    1  = @('nullptr', 'member read', '0xC0000005', 'nested call')
    2  = @('nullptr this', 'non-virtual call', 'member access', '0xC0000005')
    3  = @('qobject_cast', 'sender', 'nullptr', 'QPushButton', '0xC0000005')
    4  = @('singleton', 'shutdown', 'dangling pointer', 'nullptr', '0xC0000005')
    5  = @('uninitialized member', 'wild pointer', '0xCDCDCDCD', '0xC0000005')
    6  = @('GetProcAddress', 'nullptr', 'missing export', '0xC0000005')
    7  = @('use-after-free', 'vtable', '0xDD', 'virtual call', '0xC0000005')
    8  = @('double free', 'heap', '0xC0000374')
    9  = @('heap overflow', 'block header', '0xC0000374', 'private heap')
    10 = @('stack buffer overrun', '/GS cookie', '0xC0000409', 'fastfail')
    11 = @('stack overflow', 'unbounded recursion', '0xC00000FD')
    12 = @('dangling reference', 'QByteArray', '0xCD', '0xC0000005')
    13 = @('deleteLater', 'singleShot', 'lambda capture this', 'use-after-free')
    14 = @('parent-child', 'QObject', 'dangling pointer', 'QPointer')
    15 = @('cross-thread', 'QWidget', 'thread affinity', 'GUI thread')
    16 = @('signal', 'slot', 'recursive emit', '0xC00000FD')
    17 = @('pure virtual', '_purecall', 'destructor', '0xC0000409')
    18 = @('QModelIndex', 'internalPointer', 'dangling', 'QAbstractItemModel')
    19 = @('iterator invalidation', 'vector erase', 'nullptr dereference')
    20 = @('data race', 'unordered_map', 'rehash', 'private heap', 'heap corruption')
    21 = @('deadlock', 'AB-BA', 'QMutex', 'lock order')
    22 = @('deadlock', 'BlockingQueuedConnection', 'QMutex', 'GUI thread')
    23 = @('hang', 'CPU spin', 'main thread', 'watchdog capture')
    24 = @('hang', 'WaitForSingleObject', 'INFINITE', 'watchdog capture')
    25 = @('leak', 'heap blocks', '1 MB each', '200 MB', '!heap -stat')
    26 = @('leak', 'QObject', 'orphan', 'no parent', '3000 objects')
    27 = @('leak', 'QSharedPointer', 'reference cycle')
    28 = @('leak', 'GDI', 'HDC', 'HBITMAP', 'CreateCompatibleDC')
    29 = @('leak', 'handle', 'CreateEvent', 'kernel object')
    30 = @('leak', 'thread', '150 threads', 'never joined')
}

$cases = @()
foreach ($file in $files) {
    $full = Join-Path $repo ($file.path -replace '/', '\')
    $lines = [IO.File]::ReadAllLines($full)
    $markers = @()
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match '@fault') { $markers += ($i + 1) }
    }
    if ($markers.Count -ne $file.order.Count) {
        throw ("{0}: expected {1} @fault markers, found {2}" -f $file.path, $file.order.Count, $markers.Count)
    }
    for ($k = 0; $k -lt $markers.Count; $k++) {
        $id = $file.order[$k]
        $cases += [pscustomobject][ordered]@{
            case_id              = $id
            case_name            = $names[$id]
            dump_file            = ($names[$id] + '.dmp')
            expected_source_file = $file.path
            expected_line_approx = $markers[$k]
            root_cause_keywords  = @($keywords[$id])
        }
    }
}

$cases = $cases | Sort-Object case_id
$document = [ordered]@{
    skill       = 'windows-dump-analysis'
    lab         = 'windows-dump-lab'
    produced_by = 'CrashHarness.exe --case <id>'
    dump_dir    = 'output/dumps'
    notes       = @(
        'expected_line_approx points at the crashing statement (or the leak source) marked with a "@fault" comment in the case sources.',
        'Case 09 and 10 dumps are small by design: heap corruption (0xC0000374) is dumped with MiniDumpNormal because the heap lock is held while the exception is raised, and /GS failures go through fastfail.',
        'Regenerate with: powershell -NoProfile -File scripts\gen_expected_answers.ps1'
    )
    cases       = $cases
}

$out = Join-Path $repo 'expected_answers.json'
$json = $document | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($out, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("wrote {0} with {1} cases" -f $out, $cases.Count)
