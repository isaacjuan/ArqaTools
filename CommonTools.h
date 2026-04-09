// CommonTools.h - Shared utilities for all ArqaTools plugin modules
//
// Provides:
//   - GetModelSpace()           : open model space for writing
//   - GetEntityGroups()         : find groups containing an entity  (single-entity use)
//   - BuildEntityGroupMap()     : build reverse index entity→group  (batch use, O(G×M) once)
//   - GetEntityReferencePoint() : bounding-box centroid of any entity
//   - MoveEntityOrGroup()       : group-aware translation (two overloads)
//   - CopyEntityTo()            : deep-clone entity to a target position
//   - OpenFromEname<T>()        : typed open from selection-set index (template)
//   - SelectionSetGuard         : RAII wrapper for ads_name selection sets
//   - Message string constants  : MSG_CANCELLED, MSG_NO_SELECTION, etc.

#pragma once
#include "StdAfx.h"
#include "dbgroup.h"
#include <map>

namespace CommonTools
{
    // -------------------------------------------------------------------------
    // Message constants
    // -------------------------------------------------------------------------
    extern const TCHAR* const MSG_CANCELLED;        // "\nCommand cancelled.\n"
    extern const TCHAR* const MSG_CANCELLED_NL;     // "\nCommand cancelled."  (no trailing \n)
    extern const TCHAR* const MSG_NO_SELECTION;     // "\nNo objects selected.\n"
    extern const TCHAR* const MSG_MODEL_SPACE_ERR;  // "\nError: could not open model space.\n"

    // -------------------------------------------------------------------------
    // GetModelSpace
    // Opens model space for writing from the active working database.
    // Caller MUST call pModelSpace->close() after use.
    // -------------------------------------------------------------------------
    Acad::ErrorStatus GetModelSpace(AcDbBlockTableRecord*& pModelSpace);

    // -------------------------------------------------------------------------
    // GetEntityGroups
    // Fills groupIds with every group that contains entityId.
    // Returns true when at least one group was found.
    // O(G × M) per call — use BuildEntityGroupMap() when querying inside a loop.
    // -------------------------------------------------------------------------
    bool GetEntityGroups(AcDbObjectId entityId, AcDbObjectIdArray& groupIds);

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
    // MoveEntityOrGroup  (two overloads)
    //
    // Overload A — single-entity use, builds group index on the fly (O(G × M)).
    // Overload B — batch use, accepts a pre-built EntityGroupMap (O(1) lookup).
    //              Prefer overload B inside selection-set processing loops.
    // -------------------------------------------------------------------------
    void MoveEntityOrGroup(AcDbObjectId           objId,
                           const AcGeVector3d&     delta,
                           AcDbObjectIdArray&      processedGroups);

    void MoveEntityOrGroup(AcDbObjectId            objId,
                           const AcGeVector3d&      delta,
                           AcDbObjectIdArray&       processedGroups,
                           const EntityGroupMap&    groupMap);

    // -------------------------------------------------------------------------
    // CopyEntityTo
    // Deep-clones srcId into the current space and moves the clone so its
    // bounding-box centroid lands at targetPos.
    // Returns the clone's ObjectId, or AcDbObjectId::kNull on failure.
    // -------------------------------------------------------------------------
    AcDbObjectId CopyEntityTo(AcDbObjectId srcId, const AcGePoint3d& targetPos);

    // -------------------------------------------------------------------------
    // OpenFromEname  (template — full body in header)
    // Opens a typed entity from a selection set by index.
    // Returns Acad::eOk and sets pObj on success; pObj is nullptr on failure.
    // Caller MUST call pObj->close().
    //
    // Usage:
    //   AcDbPolyline* pPoly = nullptr;
    //   if (CommonTools::OpenFromEname(ss, i, AcDb::kForRead, pPoly) == Acad::eOk)
    //   { ... pPoly->close(); }
    // -------------------------------------------------------------------------
    template<typename T>
    Acad::ErrorStatus OpenFromEname(const ads_name   ss,
                                    Adesk::Int32     index,
                                    AcDb::OpenMode   mode,
                                    T*&              pObj)
    {
        pObj = nullptr;
        ads_name ename;
        if (acedSSName(ss, index, ename) != RTNORM)
            return Acad::eInvalidInput;
        AcDbObjectId objId;
        if (acdbGetObjectId(objId, ename) != Acad::eOk)
            return Acad::eInvalidInput;
        return acdbOpenObject(pObj, objId, mode);
    }

    // Overload that also returns the ObjectId (needed in distribute loops).
    template<typename T>
    Acad::ErrorStatus OpenFromEname(const ads_name   ss,
                                    Adesk::Int32     index,
                                    AcDb::OpenMode   mode,
                                    T*&              pObj,
                                    AcDbObjectId&    objId)
    {
        pObj = nullptr;
        ads_name ename;
        if (acedSSName(ss, index, ename) != RTNORM)
            return Acad::eInvalidInput;
        if (acdbGetObjectId(objId, ename) != Acad::eOk)
            return Acad::eInvalidInput;
        return acdbOpenObject(pObj, objId, mode);
    }

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
