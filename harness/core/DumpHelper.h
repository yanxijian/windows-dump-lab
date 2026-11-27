#pragma once

#include <string>

// Silent, self-contained dump capture for the fault lab.
//
// Init() installs every handler the CRT/Win32 offers and starts a dedicated
// dump worker thread, so that even a stack-overflow or heap-corruption fault
// can produce a dump without a WER dialog and without needing a debugger.
class DumpHelper
{
public:
    // Resolves <exe>\..\..\dumps\<caseName>.dmp, installs handlers, starts the worker.
    static void Init(const std::string& caseName);

    // Capture a dump after delayMs from a watchdog thread, then terminate the
    // process. Used by hang and leak cases that never fault on their own.
    // fullMemory=true also captures MiniDumpWithFullMemory | MiniDumpWithHandleData.
    static void StartWatchdog(const std::string& dumpName, int delayMs, bool fullMemory);

    // Absolute path of the dump this run is expected to produce.
    static std::string DumpPath();
};
