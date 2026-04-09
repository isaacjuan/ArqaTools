// CommonTools.cpp - Shared utilities for all ArqaTools plugin modules

#include "StdAfx.h"
#include "CommonTools.h"

namespace CommonTools
{
    // -------------------------------------------------------------------------
    // Message constants
    // -------------------------------------------------------------------------
    const TCHAR* const MSG_CANCELLED       = _T("\nCommand cancelled.\n");
    const TCHAR* const MSG_CANCELLED_NL    = _T("\nCommand cancelled.");
    const TCHAR* const MSG_NO_SELECTION    = _T("\nNo objects selected.\n");
    const TCHAR* const MSG_MODEL_SPACE_ERR = _T("\nError: could not open model space.\n");

    // -------------------------------------------------------------------------
    // GetModelSpace
    // -------------------------------------------------------------------------
    Acad::ErrorStatus GetModelSpace(AcDbBlockTableRecord*& pModelSpace)
    {
        pModelSpace = nullptr;
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb)
            return Acad::eNoDatabase;

        AcDbBlockTable* pBT = nullptr;
        Acad::ErrorStatus es = pDb->getBlockTable(pBT, AcDb::kForRead);
        if (es != Acad::eOk)
            return es;

        es = pBT->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForWrite);
        pBT->close();
        return es;
    }

    // -------------------------------------------------------------------------
    // GetEntityGroups
    // -------------------------------------------------------------------------
    bool GetEntityGroups(AcDbObjectId entityId, AcDbObjectIdArray& groupIds)
    {
        AcDbDatabase* pDb = entityId.database();
        if (!pDb) return false;

        AcDbDictionary* pGroupDict;
        if (pDb->getGroupDictionary(pGroupDict, AcDb::kForRead) != Acad::eOk)
            return false;

        AcDbIteratorGuard<AcDbDictionaryIterator> iter(pGroupDict->newIterator());
        for (; !iter->done(); iter->next())
        {
            AcDbObjectId groupId = iter->objectId();
            AcDbObjectGuard<AcDbGroup> group(groupId);
            if (!group) continue;

            AcDbIteratorGuard<AcDbGroupIterator> gi(group->newIterator());
            for (; !gi->done(); gi->next())
            {
                if (gi->objectId() == entityId)
                { groupIds.append(groupId); break; }
            }
        }
        pGroupDict->close();
        return (groupIds.length() > 0);
    }

    // -------------------------------------------------------------------------
    // GetEntityReferencePoint
    // -------------------------------------------------------------------------
    bool GetEntityReferencePoint(AcDbObjectId objId, AcGePoint3d& refPoint)
    {
        AcDbObjectGuard<AcDbEntity> ent(objId);
        if (!ent) return false;

        AcDbExtents extents;
        if (ent->getGeomExtents(extents) == Acad::eOk)
        { refPoint = extents.minPoint() + (extents.maxPoint() - extents.minPoint()) * 0.5; return true; }

        if (ent->isKindOf(AcDbCircle::desc()))
        { refPoint = static_cast<AcDbCircle*>(ent.get())->center(); return true; }
        if (ent->isKindOf(AcDbArc::desc()))
        { refPoint = static_cast<AcDbArc*>(ent.get())->center(); return true; }
        return false;
    }

    // -------------------------------------------------------------------------
    // BuildEntityGroupMap
    // -------------------------------------------------------------------------
    EntityGroupMap BuildEntityGroupMap(AcDbDatabase* pDb)
    {
        EntityGroupMap result;
        if (!pDb) return result;

        AcDbDictionary* pGroupDict;
        if (pDb->getGroupDictionary(pGroupDict, AcDb::kForRead) != Acad::eOk)
            return result;

        AcDbIteratorGuard<AcDbDictionaryIterator> iter(pGroupDict->newIterator());
        for (; !iter->done(); iter->next())
        {
            AcDbObjectId groupId = iter->objectId();
            AcDbObjectGuard<AcDbGroup> group(groupId);
            if (!group) continue;

            AcDbIteratorGuard<AcDbGroupIterator> gi(group->newIterator());
            for (; !gi->done(); gi->next())
                result.emplace(gi->objectId(), groupId); // first group wins on collision
        }
        pGroupDict->close();
        return result;
    }

    // -------------------------------------------------------------------------
    // MoveEntityOrGroup — shared move-group body extracted to avoid duplication
    // -------------------------------------------------------------------------
    static void MoveGroup(AcDbObjectId groupId, const AcGeMatrix3d& mat,
                          AcDbObjectIdArray& processedGroups)
    {
        if (processedGroups.contains(groupId)) return;
        processedGroups.append(groupId);

        AcDbObjectGuard<AcDbGroup> group(groupId);
        if (!group) return;

        AcDbIteratorGuard<AcDbGroupIterator> iter(group->newIterator());
        for (; !iter->done(); iter->next())
        {
            AcDbObjectGuard<AcDbEntity> ent(iter->objectId(), AcDb::kForWrite);
            if (ent) ent->transformBy(mat);
        }
    }

    // Overload A — single-entity use (O(G × M) group lookup)
    void MoveEntityOrGroup(AcDbObjectId           objId,
                           const AcGeVector3d&     delta,
                           AcDbObjectIdArray&      processedGroups)
    {
        AcGeMatrix3d mat;
        mat.setToTranslation(delta);

        AcDbObjectIdArray groupIds;
        if (GetEntityGroups(objId, groupIds) && groupIds.length() > 0)
            MoveGroup(groupIds[0], mat, processedGroups);
        else
        {
            AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
            if (ent) ent->transformBy(mat);
        }
    }

    // Overload B — batch use (O(1) map lookup)
    void MoveEntityOrGroup(AcDbObjectId            objId,
                           const AcGeVector3d&      delta,
                           AcDbObjectIdArray&       processedGroups,
                           const EntityGroupMap&    groupMap)
    {
        AcGeMatrix3d mat;
        mat.setToTranslation(delta);

        auto it = groupMap.find(objId);
        if (it != groupMap.end())
            MoveGroup(it->second, mat, processedGroups);
        else
        {
            AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
            if (ent) ent->transformBy(mat);
        }
    }

    // -------------------------------------------------------------------------
    // CopyEntityTo
    // -------------------------------------------------------------------------
    AcDbObjectId CopyEntityTo(AcDbObjectId srcId, const AcGePoint3d& targetPos)
    {
        AcGePoint3d srcCentroid;
        {
            AcDbObjectGuard<AcDbEntity> src(srcId);
            if (!src) return AcDbObjectId::kNull;
            AcDbExtents ext;
            if (src->getGeomExtents(ext) == Acad::eOk)
                srcCentroid = ext.minPoint() + (ext.maxPoint() - ext.minPoint()) * 0.5;
            else
                srcCentroid = AcGePoint3d::kOrigin;
        }

        AcGeVector3d delta = targetPos - srcCentroid;

        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        AcDbObjectIdArray srcIds;
        srcIds.append(srcId);
        AcDbIdMapping idMap;
        if (pDb->deepCloneObjects(srcIds, pDb->currentSpaceId(), idMap) != Acad::eOk)
            return AcDbObjectId::kNull;

        AcDbIdPair pair(srcId, AcDbObjectId::kNull, false);
        if (!idMap.compute(pair) || !pair.isCloned())
            return AcDbObjectId::kNull;

        AcDbObjectId cloneId = pair.value();
        {
            AcDbObjectGuard<AcDbEntity> clone(cloneId, AcDb::kForWrite);
            if (clone) { AcGeMatrix3d mat; mat.setToTranslation(delta); clone->transformBy(mat); }
        }
        return cloneId;
    }

} // namespace CommonTools
