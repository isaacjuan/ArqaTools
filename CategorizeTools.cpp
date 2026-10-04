#include "StdAfx.h"
#include "CategorizeTools.h"
#include "CommonTools.h"
#include "dbmain.h"
#include "dbents.h"
#include "dbsymtb.h"
#include <algorithm>

namespace CategorizeTools
{

// ---------------------------------------------------------------------------
DBObjectMap::DBObjectMap()
{
    populate(acdbHostApplicationServices()->workingDatabase());
}

DBObjectMap::DBObjectMap(AcDbDatabase* pDb)
{
    populate(pDb);
}

void DBObjectMap::populate(AcDbDatabase* pDb)
{
    if (!pDb)
        return;

    for (AcDbObjectId id : CommonTools::ModelSpaceIds(pDb))
    {
        AcRxClass* pClass = id.objectClass();
        std::wstring typeName = pClass ? pClass->name() : L"Unknown";
        m_objectsByType[typeName].push_back(id);
    }
}

// ---------------------------------------------------------------------------
const ObjectIdList& DBObjectMap::getObjects(const std::wstring& typeName) const
{
    static const ObjectIdList empty;
    auto it = m_objectsByType.find(typeName);
    return (it != m_objectsByType.end()) ? it->second : empty;
}

std::vector<std::wstring> DBObjectMap::getTypeNames() const
{
    std::vector<std::wstring> names;
    names.reserve(m_objectsByType.size());
    for (const auto& [name, _] : m_objectsByType)
        names.push_back(name);
    return names;
}

bool DBObjectMap::hasType(const std::wstring& typeName) const
{
    return m_objectsByType.find(typeName) != m_objectsByType.end();
}

size_t DBObjectMap::getCount(const std::wstring& typeName) const
{
    auto it = m_objectsByType.find(typeName);
    return (it != m_objectsByType.end()) ? it->second.size() : 0;
}

// ---------------------------------------------------------------------------
void catEntitiesCommand()
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    if (!pDb)
    {
        acutPrintf(_T("\nNo active database.\n"));
        return;
    }

    DBObjectMap map(pDb);

    if (map.isEmpty())
    {
        acutPrintf(_T("\nNo entities found in model space.\n"));
        return;
    }

    // Sort by count descending
    auto typeNames = map.getTypeNames();
    std::sort(typeNames.begin(), typeNames.end(),
        [&](const std::wstring& a, const std::wstring& b)
        {
            return map.getCount(a) > map.getCount(b);
        });

    acutPrintf(_T("\n=== Entity Categories ===\n"));
    acutPrintf(_T("%-40s %s\n"), _T("Type"), _T("Count"));
    acutPrintf(_T("---------------------------------------- -------\n"));

    size_t total = 0;
    for (const auto& name : typeNames)
    {
        size_t count = map.getCount(name);
        total += count;
        acutPrintf(_T("%-40s %zu\n"), name.c_str(), count);
    }

    acutPrintf(_T("---------------------------------------- -------\n"));
    acutPrintf(_T("%-40s %zu\n"), _T("TOTAL"), total);
    acutPrintf(_T("\n"));
}

} // namespace CategorizeTools
