#include "StdAfx.h"
#include "CategorizeTools.h"
#include "CommonTools.h"
#include "dbmain.h"
#include "dbents.h"
#include "dbsymtb.h"

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

} // namespace CategorizeTools
