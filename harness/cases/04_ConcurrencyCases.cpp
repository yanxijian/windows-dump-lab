// Cases 19-24: container/iterator misuse, data races, deadlocks and hangs.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <QCoreApplication>
#include <QMetaObject>
#include <QMutex>
#include <QObject>

namespace
{
    // ---------------------------------------------------------------- case 19
    // Erasing while iterating shifts a nullptr into the slot the iterator still
    // points at, so the next dereference is a null call.
    struct Item
    {
        int value = 0;

        void touch()
        {
            value += 1;
        }
    };

    void Case19()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        std::vector<Item*> items;
        items.push_back(new Item());
        items.push_back(nullptr); // sentinel that lands under the iterator after erase
        items.push_back(new Item());

        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (*it == nullptr)
            {
                continue;
            }
            items.erase(it);    // iterator invalidation: elements shift left
            (*it)->touch();     // @fault (*it is now the shifted-in nullptr)
        }

        std::printf("[case 19] loop finished without fault\n");
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 20
    // Unsynchronised writers on one unordered_map corrupt its bucket array.
    // The map's nodes come from a private heap: a race that wrecks the process
    // heap also wrecks MiniDumpWriteDump itself, and the lab needs a dump.
    HANDLE MapHeap()
    {
        static HANDLE heap = HeapCreate(0, 0, 0);
        return heap;
    }

    template <class T>
    struct MapAllocator
    {
        using value_type = T;

        MapAllocator() = default;
        template <class U>
        MapAllocator(const MapAllocator<U>&) noexcept
        {
        }

        T* allocate(std::size_t n)
        {
            void* raw = HeapAlloc(MapHeap(), 0, n * sizeof(T));
            if (raw == nullptr)
            {
                throw std::bad_alloc();
            }
            return static_cast<T*>(raw);
        }

        void deallocate(T* p, std::size_t) noexcept
        {
            HeapFree(MapHeap(), 0, p);
        }

        template <class U>
        bool operator==(const MapAllocator<U>&) const noexcept
        {
            return true;
        }
        template <class U>
        bool operator!=(const MapAllocator<U>&) const noexcept
        {
            return false;
        }
    };

    using RaceMap = std::unordered_map<int, int, std::hash<int>, std::equal_to<int>,
                                       MapAllocator<std::pair<const int, int>>>;

    void Case20()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        auto* map = new RaceMap();
        std::atomic<bool> stop{ false };

        auto hammer = [map, &stop](int seed) {
            for (int i = 0; !stop.load(std::memory_order_relaxed); ++i)
            {
                map->insert_or_assign(i * 4 + seed, i);
                if (i % 3 == 0)
                {
                    map->erase(i * 2);
                }
            }
        };

        std::thread a(hammer, 0);
        std::thread b(hammer, 1);
        std::thread c(hammer, 2);

        for (int i = 0; i < 5000; ++i)
        {
            (*map)[i] = i;   // @fault (concurrent rehash -> corrupted buckets)
            map->erase(i / 2);
        }

        stop = true;
        a.join();
        b.join();
        c.join();

        std::printf("[case 20] threads finished without fault\n");
        Sleep(INFINITE);
    }

    // ---------------------------------------------------------------- case 21
    // AB-BA lock order between the main thread and a worker.
    void Case21()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        QMutex first;
        QMutex second;

        std::thread worker([&first, &second] {
            first.lock();
            Sleep(60);
            second.lock(); // waits for the main thread
            second.unlock();
            first.unlock();
        });

        Sleep(20);
        second.lock();
        Sleep(60);
        first.lock(); // @fault (AB-BA cycle: main waits first, worker waits second)
        first.unlock();
        second.unlock();
        worker.join();
    }

    // ---------------------------------------------------------------- case 22
    // Worker holds the mutex and blocks on a BlockingQueuedConnection into the
    // GUI thread, while the GUI thread is waiting for that same mutex.
    class PingReceiver : public QObject
    {
    public:
        int hits = 0;
    };

    void Case22()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        QMutex guard;
        PingReceiver* receiver = new PingReceiver();

        std::thread worker([&guard, receiver] {
            guard.lock();
            QMetaObject::invokeMethod(receiver, [] {}, Qt::BlockingQueuedConnection); // @fault (waits for GUI)
            guard.unlock();
        });

        Sleep(20);
        guard.lock(); // GUI thread never returns to the event loop
        std::printf("[case 22] lock acquired (unexpected)\n");
        guard.unlock();
        worker.join();
    }

    // ---------------------------------------------------------------- case 23
    // Main thread stuck in a pure CPU loop.
    void Case23()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        volatile unsigned long long accumulator = 0;
        for (unsigned long long i = 0;; ++i)
        {
            accumulator += i * 3 + 1; // @fault (main thread saturates one core forever)
        }
    }

    // ---------------------------------------------------------------- case 24
    // Main thread parked in an infinite kernel wait.
    void Case24()
    {
        DumpHelper::StartWatchdog("", 1500, false);

        HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::printf("[case 24] waiting forever on %p\n", static_cast<void*>(never));
        std::fflush(stdout);
        WaitForSingleObject(never, INFINITE); // @fault (main thread never wakes up)
    }
}

REGISTER_CASE(19, "19_stl_iterator_invalidation", false, &Case19);
REGISTER_CASE(20, "20_multithread_map_race", false, &Case20);
REGISTER_CASE(21, "21_deadlock_abba_mutex", false, &Case21);
REGISTER_CASE(22, "22_deadlock_sendmessage", true, &Case22);
REGISTER_CASE(23, "23_hang_main_thread_cpu", false, &Case23);
REGISTER_CASE(24, "24_hang_main_thread_io", false, &Case24);
