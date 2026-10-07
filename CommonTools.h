// CommonTools.h - Shared utilities for all ArqaTools plugin modules
//
// Provides:
//   - GetModelSpace()           : open model space for writing
//   - BuildEntityGroupMap()     : build reverse index entity→group  (O(G×M) once)
//   - GroupUnits()              : selection → objects/groups, each group once
//   - MoveObjects()             : group-aware translation
//   - GetEntityReferencePoint() : bounding-box centroid of any entity
//   - CopyEntityTo()            : deep-clone entity to a target position
//   - SelectionSetGuard         : RAII wrapper for ads_name selection sets
//   - MSG_NO_SELECTION

#pragma once
#include "StdAfx.h"
#include "dbgroup.h"
#include <map>
#include <vector>

namespace CommonTools
{
    extern const TCHAR* const MSG_NO_SELECTION;     // "\nNo objects selected.\n"

    // -------------------------------------------------------------------------
    // GetModelSpace
    // Opens model space (for writing unless told otherwise) from the active
    // working database. Caller MUST call pModelSpace->close() after use.
    // -------------------------------------------------------------------------
    Acad::ErrorStatus GetModelSpace(AcDbBlockTableRecord*& pModelSpace,
                                    AcDb::OpenMode mode = AcDb::kForWrite);

    // All entity ids in model space, in drawing order (opened for read only).
    std::vector<AcDbObjectId> ModelSpaceIds(AcDbDatabase* pDb = nullptr);

    // -------------------------------------------------------------------------
    // AppendEntity / AppendToModelSpace - the one way to add a new entity.
    // On success the entity is closed and its id returned; on failure it is
    // deleted (it never reached the database) and kNull is returned. Either
    // way the caller must not touch pEnt afterwards.
    // AppendEntity takes an already-open block table record, for loops that
    // add many entities; AppendToModelSpace opens and closes model space itself.
    // -------------------------------------------------------------------------
    AcDbObjectId AppendEntity(AcDbBlockTableRecord* pBTR, AcDbEntity* pEnt);
    AcDbObjectId AppendToModelSpace(AcDbEntity* pEnt);

    // -------------------------------------------------------------------------
    // Selection helpers
    // SelectIds prompts with acedSSGet (optional DXF-0 type filter such as
    // _T("TEXT,MTEXT")) and returns the picked ids; empty when nothing was
    // selected or the user cancelled (cancelled tells the two apart).
    // SelectionIds converts an existing selection set.
    // -------------------------------------------------------------------------
    std::vector<AcDbObjectId> SelectIds(const TCHAR* dxfFilter = nullptr, bool* cancelled = nullptr);
    std::vector<AcDbObjectId> SelectionIds(const ads_name ss);

    // -------------------------------------------------------------------------
    // Small shared queries
    // -------------------------------------------------------------------------
    // Hex handle string <-> ObjectId (pDb defaults to the working database).
    CString      HandleString(AcDbObjectId id);
    AcDbObjectId IdFromHandle(const CString& hex, AcDbDatabase* pDb = nullptr);

    // Full length of any curve (line, arc, polyline, spline...).
    bool CurveLength(const AcDbCurve* pCurve, double& length);

    // Name of the block a reference points at ("" if it cannot be read).
    CString BlockName(const AcDbBlockReference* pRef);

    // -------------------------------------------------------------------------
    // EntityGroupMap / BuildEntityGroupMap
    //
    // EntityGroupMap is a reverse index: entity ID → primary (first) group ID.
    // Built in one O(G × M) pass over the group dictionary.  All subsequent
    // group lookups within the same command are O(1) map::find() calls.
    //
    // Usage pattern in entity-processing loops:
    //   auto gmap = CommonTools::BuildEntityGroupMap(pDb);
    //   for (each entity in selection)
    //   {
    //       auto it = gmap.find(objId);
    //       if (it != gmap.end()) { /* it->second is the group ID */ }
    //   }
    // -------------------------------------------------------------------------
    using EntityGroupMap = std::map<AcDbObjectId, AcDbObjectId>;
    EntityGroupMap BuildEntityGroupMap(AcDbDatabase* pDb);

    // -------------------------------------------------------------------------
    // GetEntityReferencePoint
    // Returns the bounding-box centroid of objId.
    // Falls back to center() for AcDbCircle / AcDbArc when extents fail.
    // Returns false if the point cannot be determined.
    // -------------------------------------------------------------------------
    bool GetEntityReferencePoint(AcDbObjectId objId, AcGePoint3d& refPoint);

    // -------------------------------------------------------------------------
    // GroupUnits / MoveObjects
    //
    // A selection resolved into the units that move or copy together: an
    // entity in a group becomes its whole group (once, however many members
    // are selected); any other entity stays alone (groupId kNull). `picked`
    // is the first selected entity of the unit.
    // -------------------------------------------------------------------------
    struct GroupUnit
    {
        AcDbObjectId              picked;
        AcDbObjectId              groupId;
        std::vector<AcDbObjectId> members;
    };
    std::vector<GroupUnit> GroupUnits(const std::vector<AcDbObjectId>& ids);

    // Translates exactly these entities.
    void TranslateEntities(const std::vector<AcDbObjectId>& ids, const AcGeVector3d& delta);

    // Moves each entity, or its whole group once.
    void MoveObjects(const std::vector<AcDbObjectId>& ids, const AcGeVector3d& delta);

    // -------------------------------------------------------------------------
    // CopyEntityTo
    // Deep-clones srcId into the current space and moves the clone so its
    // bounding-box centroid lands at targetPos.
    // Returns the clone's ObjectId, or AcDbObjectId::kNull on failure.
    // -------------------------------------------------------------------------
    AcDbObjectId CopyEntityTo(AcDbObjectId srcId, const AcGePoint3d& targetPos);

    // -------------------------------------------------------------------------
    // AcDbObjectGuard<T>
    // RAII wrapper for any AcDbObject subclass opened with acdbOpenObject.
    // The close() call is guaranteed on all exit paths — early returns,
    // exceptions, break out of loops, end of scope — with zero boilerplate.
    //
    // Usage (replaces the nullptr + acdbOpenObject + check + close() idiom):
    //   // Pattern A — must succeed, else return:
    //   AcDbObjectGuard<AcDbPolyline> poly(polylineId);
    //   if (!poly) { acutPrintf(_T("\nError")); return; }
    //   poly->isClosed(); ...
    //
    //   // Pattern B — optional block:
    //   { AcDbObjectGuard<AcDbGroup> grp(groupId);
    //     if (grp) { grp->numEntities(); ... } }
    //
    //   // Write mode:
    //   AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
    //   if (ent) { ent->transformBy(mat); }
    // -------------------------------------------------------------------------
    template<typename T>
    class AcDbObjectGuard
    {
    public:
        T* ptr = nullptr;

        AcDbObjectGuard() = default;

        explicit AcDbObjectGuard(AcDbObjectId id,
                                 AcDb::OpenMode mode = AcDb::kForRead)
        {
            if (!id.isNull())
                acdbOpenObject(ptr, id, mode);
        }

        ~AcDbObjectGuard() { if (ptr) ptr->close(); }

        T*       get()  const { return ptr; }
        T*       operator->() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }

        // Non-copyable: ownership of the open handle is unique.
        AcDbObjectGuard(const AcDbObjectGuard&)            = delete;
        AcDbObjectGuard& operator=(const AcDbObjectGuard&) = delete;
    };

    // -------------------------------------------------------------------------
    // ForEachSsEntity  (template — full body in header)
    // Iterate a selection set, safely resolving each entry to an AcDbObjectId.
    // Skips entries where acedSSName or acdbGetObjectId fails (both error paths
    // that callers previously handled inconsistently or silently ignored).
    //
    // Usage (replaces the 3-line ads_name/acedSSName/acdbGetObjectId boilerplate):
    //   CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId objId)
    //   {
    //       // process objId — use return instead of continue to skip
    //   });
    // -------------------------------------------------------------------------
    template<typename Fn>
    void ForEachSsEntity(const ads_name ss, Adesk::Int32 length, Fn&& fn)
    {
        for (Adesk::Int32 i = 0; i < length; i++)
        {
            ads_name ename;
            if (acedSSName(ss, i, ename) != RTNORM) continue;
            AcDbObjectId objId;
            if (acdbGetObjectId(objId, ename) != Acad::eOk) continue;
            fn(objId);
        }
    }

    // -------------------------------------------------------------------------
    // AcDbIteratorGuard<T>
    // RAII wrapper for any ObjectARX iterator (AcDbDictionaryIterator,
    // AcDbGroupIterator, AcDbBlockTableRecordIterator, …).
    // Guarantees delete on all exit paths; exposes operator-> for full API.
    //
    // Usage:
    //   AcDbIteratorGuard<AcDbGroupIterator> it(group->newIterator());
    //   for (; !it->done(); it->next())
    //       AcDbObjectId id = it->objectId();
    // -------------------------------------------------------------------------
    template<typename T>
    class AcDbIteratorGuard
    {
        T* ptr;
    public:
        explicit AcDbIteratorGuard(T* p) : ptr(p) {}
        ~AcDbIteratorGuard() { delete ptr; }
        T* operator->() const { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }

        AcDbIteratorGuard(const AcDbIteratorGuard&)            = delete;
        AcDbIteratorGuard& operator=(const AcDbIteratorGuard&) = delete;
    };

    // -------------------------------------------------------------------------
    // AcDbDictionaryGuard
    // RAII wrapper for AcDbDictionary* (calls close() on destruction).
    //
    // Usage:
    //   AcDbDictionary* pRaw;
    //   if (pDb->getGroupDictionary(pRaw, AcDb::kForRead) != Acad::eOk) return;
    //   AcDbDictionaryGuard dict(pRaw);
    //   AcDbIteratorGuard<AcDbDictionaryIterator> it(dict->newIterator());
    // -------------------------------------------------------------------------
    class AcDbDictionaryGuard
    {
        AcDbDictionary* ptr;
    public:
        explicit AcDbDictionaryGuard(AcDbDictionary* p) : ptr(p) {}
        ~AcDbDictionaryGuard() { if (ptr) ptr->close(); }
        AcDbDictionary* operator->() const { return ptr; }
        AcDbDictionary* get()         const { return ptr; }
        explicit operator bool()      const { return ptr != nullptr; }

        AcDbDictionaryGuard(const AcDbDictionaryGuard&)            = delete;
        AcDbDictionaryGuard& operator=(const AcDbDictionaryGuard&) = delete;
    };

    // -------------------------------------------------------------------------
    // SelectionSetGuard
    // RAII wrapper for ads_name selection sets.
    // Calls acedSSFree automatically on destruction (all exit paths).
    //
    // Usage:
    //   CommonTools::SelectionSetGuard ssGuard;
    //   if (!ssGuard.Get()) { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }
    //   Adesk::Int32 len;
    //   acedSSLength(ssGuard.ss, &len);
    //   for (Adesk::Int32 i = 0; i < len; i++) { ... ssGuard.ss ... }
    // -------------------------------------------------------------------------
    class SelectionSetGuard
    {
    public:
        ads_name ss;
        bool     acquired = false;

        SelectionSetGuard() = default;

        // Non-copyable (ads_name is a raw array).
        SelectionSetGuard(const SelectionSetGuard&)            = delete;
        SelectionSetGuard& operator=(const SelectionSetGuard&) = delete;

        ~SelectionSetGuard()
        {
            if (acquired)
                acedSSFree(ss);
        }

        // Call acedSSGet with no filter and record the result.
        // Returns true when a non-empty selection was obtained.
        bool Get()
        {
            acquired = (acedSSGet(NULL, NULL, NULL, NULL, ss) == RTNORM);
            return acquired;
        }

        // Call acedSSGet with a custom prompt string and no filter.
        bool Get(const TCHAR* prompt)
        {
            acquired = (acedSSGet(_T(":S"), NULL, NULL, NULL, ss) == RTNORM);
            (void)prompt;  // prompt shown by caller via acutPrintf before calling
            return acquired;
        }
    };

} // namespace CommonTools
