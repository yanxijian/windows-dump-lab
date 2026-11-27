// Cases 07-12: use-after-free, double free, buffer overruns and stack exhaustion.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <QByteArray>

namespace
{
    // ---------------------------------------------------------------- case 07
    // delete without nulling, the freed block is recycled and overwritten with
    // 0xDD, then a virtual call dereferences the smashed vtable.
    struct Shape
    {
        virtual ~Shape() = default;
        virtual int area() const { return 7; }
        int payload[4]{};
    };

    struct Square : Shape
    {
        int area() const override { return 42; }
    };

    void Case07()
    {
        Shape* shape = new Square();
        delete shape; // dangling, not nulled
        for (int i = 0; i < 64; ++i)
        {
            void* junk = new char[sizeof(Square)];
            std::memset(junk, 0xDD, sizeof(Square));
        }
        volatile int area = shape->area(); // @fault (vtable == 0xDDDDDDDDDDDDDDDD)
        (void)area;
    }

    // ---------------------------------------------------------------- case 08
    void Case08()
    {
        int* block = new int[8];
        std::memset(block, 0x11, 8 * sizeof(int));
        delete[] block;
        delete[] block; // @fault (double free)
    }

    // ---------------------------------------------------------------- case 09
    // 32-byte allocation, 128-byte write. The overwrite deliberately reaches the
    // block header (user data starts 0x10 into the entry on x64), because freeing
    // a block only validates that block's own header. The block lives on a private
    // heap: RtlFreeHeap raises 0xC0000374 while holding that heap's lock, so
    // corrupting the process heap would deadlock the dump writer.
    void Case09()
    {
        HANDLE privateHeap = HeapCreate(0, 0, 0);
        char* tiny = static_cast<char*>(HeapAlloc(privateHeap, 0, 32));
        volatile char* raw = tiny - 16; // block header, x64
        for (int i = 0; i < 16 + 32 + 128; ++i)
        {
            raw[i] = static_cast<char>(0x41);
        }
        HeapFree(privateHeap, 0, tiny); // @fault (0xC0000374, heap corruption detected)
    }

    // ---------------------------------------------------------------- case 10
    // Stack buffer overrun smashing the /GS cookie. A real /GS failure reaches
    // __fastfail(int 0x29), which bypasses VEH and SEH, so user mode cannot take
    // a dump of it; the same status code is raised explicitly here so the lab
    // still produces an analysable dump with the smashed frame on the stack.
    void Case10()
    {
        volatile char buffer[16];
        for (int i = 0; i < 64; ++i)
        {
            buffer[i] = static_cast<char>(0x41); // overwrites saved frame + stack cookie
        }
        RaiseException(0xC0000409, EXCEPTION_NONCONTINUABLE, 0, nullptr); // @fault
    }

    // ---------------------------------------------------------------- case 11
    volatile int g_recursionSink = 0;

    void RecurseForever(int depth)
    {
        volatile char pad[1024];
        pad[0] = static_cast<char>(depth & 0x7F);
        g_recursionSink += pad[0];
        RecurseForever(depth + 1); // @fault (stack exhausted -> 0xC00000FD)
    }

    void Case11()
    {
        RecurseForever(0);
    }

    // ---------------------------------------------------------------- case 12
    // Returning a const reference to a local QByteArray: the reference outlives
    // the object. Its destroyed storage is reused, then the next access goes
    // through the corrupted internal data pointer.
    const QByteArray& MakePayload()
    {
        QByteArray local(64, 'x');
        return local; // dangling reference
    }

    void Case12()
    {
        const QByteArray& payload = MakePayload();
        volatile unsigned char* raw =
            reinterpret_cast<volatile unsigned char*>(const_cast<QByteArray*>(&payload));
        for (size_t i = 0; i < sizeof(QByteArray); ++i)
        {
            raw[i] = 0xCD; // the destroyed object's storage is reused
        }
        volatile char first = payload.at(0); // @fault (internal data pointer == 0xCDCD...)
        (void)first;
    }
}

REGISTER_CASE(7, "07_cpp_direct_uaf_vtable", false, &Case07);
REGISTER_CASE(8, "08_double_free_heap", false, &Case08);
REGISTER_CASE(9, "09_heap_buffer_overflow", false, &Case09);
REGISTER_CASE(10, "10_stack_buffer_overflow", false, &Case10);
REGISTER_CASE(11, "11_stack_overflow_recursion", false, &Case11);
REGISTER_CASE(12, "12_dangling_reference_return", false, &Case12);
