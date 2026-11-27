// Cases 01-06: null / invalid pointer faults.
// Each case crashes on a specific line marked with a fault comment, so that
// expected_answers.json can be generated from the sources.

#include "core/CaseRegistry.h"
#include "core/DumpHelper.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <malloc.h>

#include <QObject>
#include <QPushButton>
#include <QString>

namespace
{
    // ---------------------------------------------------------------- case 01
    struct RenderConfig
    {
        int width;
        int height;
        int depth;
    };

    int ReadDepth(RenderConfig* config)
    {
        return config->depth; // @fault (config == nullptr)
    }

    int ReadDepthLevel2(RenderConfig* config)
    {
        return ReadDepth(config) + 1;
    }

    int ReadDepthLevel3(RenderConfig* config)
    {
        return ReadDepthLevel2(config) * 2;
    }

    void Case01()
    {
        RenderConfig* config = nullptr; // never built because of an early-return path
        volatile int depth = ReadDepthLevel3(config);
        (void)depth;
    }

    // ---------------------------------------------------------------- case 02
    // Non-virtual call through nullptr: the fault happens on the first member
    // access inside the callee, not at the call site.
    struct Session
    {
        int id;
        char label[32];

        void Touch();
    };

    void Session::Touch()
    {
        label[0] = 'x'; // @fault (this == nullptr)
        id = 1;
    }

    void Case02()
    {
        Session* session = nullptr;
        session->Touch();
    }

    // ---------------------------------------------------------------- case 03
    // qobject_cast on sender() returns nullptr outside a signal delivery; the
    // result is used without a null check.
    class ButtonGadget : public QObject
    {
    public:
        void fire()
        {
            QPushButton* button = qobject_cast<QPushButton*>(sender());
            button->setText(QStringLiteral("boom")); // @fault (button == nullptr)
        }
    };

    void Case03()
    {
        ButtonGadget gadget;
        gadget.fire();
    }

    // ---------------------------------------------------------------- case 04
    // Shutdown order: the singleton is destroyed and nulled, but a background
    // callback still fires afterwards.
    struct TelemetryService
    {
        int counter = 0;

        void Refresh()
        {
            ++counter;
        }
    };

    TelemetryService* g_telemetry = nullptr;

    void BackgroundTick()
    {
        g_telemetry->Refresh(); // @fault (g_telemetry == nullptr)
    }

    void Case04()
    {
        g_telemetry = new TelemetryService();
        g_telemetry->Refresh();
        delete g_telemetry;
        g_telemetry = nullptr; // simulated shutdown
        BackgroundTick();
    }

    // ---------------------------------------------------------------- case 05
    // An uninitialised member pointer keeps a wild address. 0xCD is what the
    // debug CRT fills fresh allocations with, so the fault address is stable.
    struct SampleBuffer
    {
        int* samples;
        int count;
    };

    void Case05()
    {
        void* raw = _aligned_malloc(sizeof(SampleBuffer), 8);
        std::memset(raw, 0xCD, sizeof(SampleBuffer));
        SampleBuffer* buffer = static_cast<SampleBuffer*>(raw);
        volatile int first = buffer->samples[0]; // @fault (samples == 0xCDCDCDCDCDCDCDCD)
        (void)first;
    }

    // ---------------------------------------------------------------- case 06
    // GetProcAddress returns nullptr for a missing export; the result is cast
    // to a function pointer and invoked.
    void Case06()
    {
        using QueryFn = int (*)(int);
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        QueryFn query = reinterpret_cast<QueryFn>(
            GetProcAddress(kernel32, "ThisExportDoesNotExist_9C1F")); // always nullptr
        volatile int result = query(1); // @fault
        (void)result;
    }
}

REGISTER_CASE(1, "01_nullptr_member_read", false, &Case01);
REGISTER_CASE(2, "02_nullptr_virtual_call", false, &Case02);
REGISTER_CASE(3, "03_qt_qobject_cast_null", true, &Case03);
REGISTER_CASE(4, "04_singleton_shutdown_null", false, &Case04);
REGISTER_CASE(5, "05_uninitialized_wild_ptr", false, &Case05);
REGISTER_CASE(6, "06_dll_getprocaddress_null", false, &Case06);
