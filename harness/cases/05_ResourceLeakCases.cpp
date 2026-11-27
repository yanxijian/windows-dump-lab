// Cases 25-30: heap, QObject, QSharedPointer, GDI, kernel handle and thread leaks.
// Every case arms the watchdog with full memory + handle data so the dump can be
// used for !heap / !handle / !address analysis.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include <QObject>
#include <QSharedPointer>

namespace
{
    // ---------------------------------------------------------------- case 25
    void Case25()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        static std::vector<char*> blocks;
        for (int i = 0; i < 200; ++i)
        {
            char* block = new char[1024 * 1024];
            std::memset(block, 0xA5, 1024 * 1024);
            blocks.push_back(block); // @fault (leak source: 1 MB blocks never released)
        }

        std::printf("[case 25] leaked %zu MB\n", blocks.size());
        std::fflush(stdout);
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 26
    class OrphanWidget : public QObject
    {
    public:
        int payload[8]{};
    };

    void Case26()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        static std::vector<OrphanWidget*> orphans;
        for (int i = 0; i < 3000; ++i)
        {
            auto* orphan = new OrphanWidget(); // no parent, never deleted
            orphan->payload[0] = i;
            orphans.push_back(orphan);         // @fault (leak source: orphan QObjects, no parent, never deleted)
        }

        std::printf("[case 26] leaked %zu orphan QObjects\n", orphans.size());
        std::fflush(stdout);
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 27
    // peer points at another node that points straight back: neither strong
    // count ever reaches zero, so both objects stay alive forever.
    struct CycleNode
    {
        QSharedPointer<CycleNode> peer;
        int payload[4]{};
    };

    void Case27()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        static std::vector<QSharedPointer<CycleNode>> keepAlive;
        for (int i = 0; i < 500; ++i)
        {
            QSharedPointer<CycleNode> a = QSharedPointer<CycleNode>::create();
            QSharedPointer<CycleNode> b = QSharedPointer<CycleNode>::create();
            a->peer = b; // strong
            b->peer = a; // strong -> reference cycle
            keepAlive.push_back(a); // @fault (leak source: a->peer / b->peer keep each other alive)
        }

        std::printf("[case 27] created %zu reference cycles\n", keepAlive.size());
        std::fflush(stdout);
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 28
    void Case28()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        for (int i = 0; i < 2000; ++i)
        {
            HDC dc = CreateCompatibleDC(nullptr);        // @fault (leak source: HDC never deleted)
            HBITMAP bitmap = CreateBitmap(1, 1, 1, 1, nullptr); // never DeleteObject
            if (dc != nullptr && bitmap != nullptr)
            {
                SelectObject(dc, bitmap);
            }
        }

        std::printf("[case 28] created 2000 DC + 2000 bitmap handles\n");
        std::fflush(stdout);
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 29
    void Case29()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        static std::vector<HANDLE> events;
        for (int i = 0; i < 2000; ++i)
        {
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); // @fault (leak source: event never closed)
            if (event != nullptr)
            {
                events.push_back(event);
            }
        }

        std::printf("[case 29] leaked %zu kernel event handles\n", events.size());
        std::fflush(stdout);
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 30
    void Case30()
    {
        DumpHelper::StartWatchdog("", 1500, true);

        static std::vector<std::thread> zombies;
        for (int i = 0; i < 150; ++i)
        {
            zombies.emplace_back([] { // @fault (leak source: thread never joined)
                Sleep(INFINITE); // parked forever, never joined
            });
        }

        std::printf("[case 30] parked %zu threads\n", zombies.size());
        std::fflush(stdout);
        Sleep(INFINITE);
    }
}

REGISTER_CASE(25, "25_leak_cpp_heap_blocks", false, &Case25);
REGISTER_CASE(26, "26_leak_qt_orphan_qobjects", false, &Case26);
REGISTER_CASE(27, "27_leak_qsharedpointer_cycle", false, &Case27);
REGISTER_CASE(28, "28_leak_gdi_handles", false, &Case28);
REGISTER_CASE(29, "29_leak_kernel_event_handles", false, &Case29);
REGISTER_CASE(30, "30_leak_zombie_threads", false, &Case30);
