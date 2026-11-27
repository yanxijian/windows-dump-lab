// windows-dump-lab: 30 fault scenarios for exercising the windows-dump-analysis skill.
//
//   CrashHarness.exe --case <1..30>    run one scenario and produce its dump
//   CrashHarness.exe --list            print every scenario
//
// Scenarios either fault (the dump helper captures the exception) or hang/leak
// (a watchdog thread captures the dump and terminates). The process never shows
// a WER dialog and never needs a debugger attached.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <QApplication>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::printf("usage: CrashHarness --case <1..30>\n");
        std::printf("       CrashHarness --list\n");
    }

    void PrintCases()
    {
        for (const CaseInfo& info : AllCases())
        {
            std::printf("%2d  %-36s %s\n", info.id, info.name, info.needsQt ? "qt" : "native");
        }
    }

    int ParseCaseId(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--list") == 0)
            {
                return -1;
            }
            if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            {
                return std::atoi(argv[i + 1]);
            }
            if (std::strncmp(argv[i], "--case=", 7) == 0)
            {
                return std::atoi(argv[i] + 7);
            }
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    int id = ParseCaseId(argc, argv);
    if (id == -1)
    {
        PrintCases();
        return 0;
    }
    if (id == 0)
    {
        PrintUsage();
        PrintCases();
        return 2;
    }

    const CaseInfo* info = FindCase(id);
    if (info == nullptr)
    {
        std::printf("unknown case: %d\n", id);
        return 2;
    }

    // The dump name is derived from the case name, so the file lands as
    // output/dumps/<id>_<name>.dmp no matter where the process was started.
    DumpHelper::Init(info->name);

    std::printf("[case %02d] %s\n", info->id, info->name);
    std::fflush(stdout);

    if (info->needsQt)
    {
        QApplication app(argc, argv);
        info->run();
    }
    else
    {
        info->run();
    }

    // Reaching this point means the scenario did not fault; hang/leak cases arm
    // the watchdog and block, so a clean exit here is a lab bug rather than a
    // crash. Say so explicitly instead of exiting silently.
    std::printf("[case %02d] finished without fault (unexpected for a fault case)\n", info->id);
    std::fflush(stdout);
    return 0;
}
