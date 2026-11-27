#pragma once

#include <algorithm>
#include <string>
#include <vector>

// One fault-lab scenario. `name` doubles as the dump base name, so it must be
// "<two-digit id>_<snake_case_name>" and unique.
struct CaseInfo
{
    int id;
    const char* name;
    bool needsQt;   // true: main() creates a QApplication before running the case
    void (*run)();
};

inline std::vector<CaseInfo>& CaseTable()
{
    static std::vector<CaseInfo> table;
    return table;
}

struct CaseRegistrar
{
    explicit CaseRegistrar(const CaseInfo& info) { CaseTable().push_back(info); }
};

#define REGISTER_CASE(id, name, needsQt, fn) \
    static CaseRegistrar g_caseRegistrar_##id(CaseInfo{ id, name, needsQt, fn })

inline const std::vector<CaseInfo>& AllCases()
{
    static std::vector<CaseInfo> sorted;
    if (sorted.empty())
    {
        sorted = CaseTable();
        std::sort(sorted.begin(), sorted.end(),
                  [](const CaseInfo& a, const CaseInfo& b) { return a.id < b.id; });
    }
    return sorted;
}

inline const CaseInfo* FindCase(int id)
{
    for (const CaseInfo& info : AllCases())
    {
        if (info.id == id)
        {
            return &info;
        }
    }
    return nullptr;
}
