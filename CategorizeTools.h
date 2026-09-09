#pragma once
#include "StdAfx.h"
#include <map>
#include <vector>
#include <string>

// ============================================================================
// CategorizeTools — Entity categorization for ArqaTools
//
// Mirrors the DBObjectMap functionality from Simpson EntityCategorizer.h
// using the AcDb* API (IcArx bridge).
// ============================================================================
namespace CategorizeTools
{
    using ObjectIdList  = std::vector<AcDbObjectId>;
    using TypeObjectMap = std::map<std::wstring, ObjectIdList>;

    /// Scans model space and groups entities by class name.
    class DBObjectMap
    {
    public:
        DBObjectMap();
        explicit DBObjectMap(AcDbDatabase* pDb);

        const ObjectIdList& getObjects(const std::wstring& typeName) const;
        std::vector<std::wstring> getTypeNames() const;
        bool   hasType(const std::wstring& typeName) const;
        size_t getCount(const std::wstring& typeName) const;
        const  TypeObjectMap& getObjectsByType() const { return m_objectsByType; }
        bool   isEmpty() const { return m_objectsByType.empty(); }

    private:
        void populate(AcDbDatabase* pDb);

        TypeObjectMap  m_objectsByType;
    };

    // Command: CATENTITIES — prints entity type counts to the command line
    void catEntitiesCommand();
}
