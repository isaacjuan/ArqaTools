// AlignTools.cpp - Object Alignment Tools Implementation

#include "StdAfx.h"
#include "HelloWorld.h"
#include "AlignTools.h"
#include "CommonTools.h"
#include "dbmtext.h"
#include "dbregion.h"

namespace AlignTools
{


    // -------------------------------------------------------------------------
    // AxisDelta: displacement vector that moves 'from' to 'coord' on one axis.
    // -------------------------------------------------------------------------
    static AcGeVector3d AxisDelta(int axis, double coord, const AcGePoint3d& from)
    {
        if (axis == 0) return AcGeVector3d(coord - from.x, 0, 0);
        if (axis == 1) return AcGeVector3d(0, coord - from.y, 0);
        return             AcGeVector3d(0, 0, coord - from.z);
    }

    // -------------------------------------------------------------------------
    // SetAxisCoord: set one coordinate component of a point in place.
    // -------------------------------------------------------------------------
    static void SetAxisCoord(AcGePoint3d& pt, int axis, double coord)
    {
        if      (axis == 0) pt.x = coord;
        else if (axis == 1) pt.y = coord;
        else                pt.z = coord;
    }

    // -------------------------------------------------------------------------
    // MoveEntityPreservingType: type-aware translation.
    // AcDbCircle / AcDbText use property setters to preserve internal state;
    // all other types use the generic transformBy.
    // -------------------------------------------------------------------------
    static void MoveEntityPreservingType(AcDbEntity* pEnt, const AcGeVector3d& delta)
    {
        if (pEnt->isKindOf(AcDbCircle::desc()))
        {
            AcDbCircle* p = static_cast<AcDbCircle*>(pEnt);
            p->setCenter(p->center() + delta);
        }
        else if (pEnt->isKindOf(AcDbText::desc()))
        {
            AcDbText* p = static_cast<AcDbText*>(pEnt);
            p->setPosition(p->position() + delta);
            p->setAlignmentPoint(p->alignmentPoint() + delta);
        }
        else
        {
            pEnt->transformBy(AcGeMatrix3d::translation(delta));
        }
    }

    // -------------------------------------------------------------------------
    // GetGroupCircleCenter: returns the center of the first AcDbCircle found
    // inside a group. Used as the alignment reference point for SEQNUM groups.
    // -------------------------------------------------------------------------
    static bool GetGroupCircleCenter(AcDbGroup* pGroup, AcGePoint3d& center)
    {
        CommonTools::AcDbIteratorGuard<AcDbGroupIterator> iter(pGroup->newIterator());
        bool found = false;
        for (; !iter->done(); iter->next())
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(iter->objectId());
            if (ent && ent->isKindOf(AcDbCircle::desc()))
            {
                center = static_cast<AcDbCircle*>(ent.get())->center();
                found = true;
                break;
            }
        }
        return found;
    }

    // -------------------------------------------------------------------------
    // AlignGroup: align every entity in a group along one axis to coord.
    // Uses the group's first circle as the reference point.
    // -------------------------------------------------------------------------
    static void AlignGroup(AcDbObjectId groupId, int axis, double coord,
                           int idx, int& aligned)
    {
        CommonTools::AcDbObjectGuard<AcDbGroup> group(groupId);
        if (!group) return;

        TCHAR groupName[256];
        _tcscpy_s(groupName, 256, group->name());
        int numBefore = group->numEntities();
        acutPrintf(_T("  [GROUP] Processing '%s' (%d entities)\n"), groupName, numBefore);

        AcGePoint3d circleCenter;
        if (!GetGroupCircleCenter(group.get(), circleCenter)) return;

        AcGeVector3d delta = AxisDelta(axis, coord, circleCenter);

        int moved = 0;
        CommonTools::AcDbIteratorGuard<AcDbGroupIterator> iter(group->newIterator());
        for (; !iter->done(); iter->next())
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(iter->objectId(), AcDb::kForWrite);
            if (ent) { MoveEntityPreservingType(ent.get(), delta); moved++; }
        }

        int numAfter = group->numEntities();
        if (numAfter != numBefore)
            acutPrintf(_T("  *** WARNING: '%s' entity count %d→%d ***\n"),
                       groupName, numBefore, numAfter);

        acutPrintf(_T("  [%d] Group '%s' aligned (%d entities)\n"), idx + 1, groupName, moved);
        aligned++;
    }

    // -------------------------------------------------------------------------
    // AlignEntity: align a single entity (not in a group) along one axis.
    // Dispatches by entity type; falls back to bounding-box min-point.
    // -------------------------------------------------------------------------
    static void AlignEntity(AcDbObjectId objId, int axis, double coord,
                            int idx, int& aligned, int& skipped)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
        if (!ent) { skipped++; return; }
        AcDbEntity* pEnt = ent.get();

        bool modified = false;

        if (pEnt->isKindOf(AcDbCircle::desc()))
        {
            AcDbCircle* p = static_cast<AcDbCircle*>(pEnt);
            AcGePoint3d c = p->center();
            SetAxisCoord(c, axis, coord);
            p->setCenter(c);
            modified = true;
            acutPrintf(_T("  [%d] Circle aligned by center\n"), idx + 1);
        }
        else if (pEnt->isKindOf(AcDbArc::desc()))
        {
            AcDbArc* p = static_cast<AcDbArc*>(pEnt);
            AcGePoint3d c = p->center();
            SetAxisCoord(c, axis, coord);
            p->setCenter(c);
            modified = true;
            acutPrintf(_T("  [%d] Arc aligned by center\n"), idx + 1);
        }
        else if (pEnt->isKindOf(AcDbCurve::desc()))
        {
            AcDbCurve* p = static_cast<AcDbCurve*>(pEnt);
            AcGePoint3d start;
            if (p->getStartPoint(start) == Acad::eOk)
            {
                p->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, start)));
                modified = true;
                acutPrintf(_T("  [%d] Curve aligned by start point\n"), idx + 1);
            }
        }
        else if (pEnt->isKindOf(AcDbText::desc()))
        {
            AcDbText* p = static_cast<AcDbText*>(pEnt);
            AcGePoint3d pos = p->position();
            SetAxisCoord(pos, axis, coord);
            p->setPosition(pos);
            modified = true;
            acutPrintf(_T("  [%d] Text aligned by position\n"), idx + 1);
        }
        else if (pEnt->isKindOf(AcDbMText::desc()))
        {
            AcDbMText* p = static_cast<AcDbMText*>(pEnt);
            AcGePoint3d loc = p->location();
            SetAxisCoord(loc, axis, coord);
            p->setLocation(loc);
            modified = true;
            acutPrintf(_T("  [%d] MText aligned by location\n"), idx + 1);
        }
        else if (pEnt->isKindOf(AcDbBlockReference::desc()))
        {
            AcDbBlockReference* p = static_cast<AcDbBlockReference*>(pEnt);
            AcGePoint3d pos = p->position();
            SetAxisCoord(pos, axis, coord);
            p->setPosition(pos);
            modified = true;
            acutPrintf(_T("  [%d] Block aligned by position\n"), idx + 1);
        }
        else if (pEnt->isKindOf(AcDbRegion::desc()))
        {
            AcDbRegion* p = static_cast<AcDbRegion*>(pEnt);
            AcDbVoidPtrArray curves;
            if (p->explode(curves) == Acad::eOk && curves.length() > 0)
            {
                AcDbCurve* pFirst = AcDbCurve::cast(static_cast<AcDbEntity*>(curves[0]));
                AcGePoint3d refPt;
                if (pFirst && pFirst->getStartPoint(refPt) == Acad::eOk)
                {
                    p->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, refPt)));
                    modified = true;
                    acutPrintf(_T("  [%d] Region aligned by boundary\n"), idx + 1);
                }
                for (int j = 0; j < curves.length(); j++)
                    delete static_cast<AcDbEntity*>(curves[j]);
            }
            else
                acutPrintf(_T("  [%d] Region - could not explode\n"), idx + 1);
        }
        else
        {
            AcDbExtents ext;
            if (pEnt->getGeomExtents(ext) == Acad::eOk)
            {
                pEnt->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, ext.minPoint())));
                modified = true;
                acutPrintf(_T("  [%d] Entity aligned by extents\n"), idx + 1);
            }
        }

        if (modified) aligned++; else skipped++;
    }

    // -------------------------------------------------------------------------
    // VerifySeqNumGroups: post-operation integrity check on all SEQNUM_ groups.
    // -------------------------------------------------------------------------
    static void VerifySeqNumGroups()
    {
        acutPrintf(_T("\n=== VERIFYING GROUP INTEGRITY ===\n"));
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        AcDbDictionary* pGroupDictRaw;
        if (pDb->getGroupDictionary(pGroupDictRaw, AcDb::kForRead) != Acad::eOk)
            return;
        CommonTools::AcDbDictionaryGuard pGroupDict(pGroupDictRaw);

        CommonTools::AcDbIteratorGuard<AcDbDictionaryIterator> iter(pGroupDict->newIterator());
        int broken = 0;
        for (; !iter->done(); iter->next())
        {
            if (_tcsstr(iter->name(), _T("SEQNUM_")) == nullptr)
                continue;

            CommonTools::AcDbObjectGuard<AcDbGroup> grp(iter->objectId());
            if (grp)
            {
                int n = grp->numEntities();
                if (n < 2)
                { acutPrintf(_T("  *** ALERT: '%s' broken! %d entities ***\n"), iter->name(), n); broken++; }
                else
                    acutPrintf(_T("  OK: '%s' has %d entities\n"), iter->name(), n);
            }
        }

        if (broken > 0)
            acutPrintf(_T("\n*** WARNING: %d SEQNUM groups broken ***\n"), broken);
        else
            acutPrintf(_T("\nAll SEQNUM groups intact.\n"));
    }

    // -------------------------------------------------------------------------
    // AlignObjectsToCoordinate: select objects and align them along one axis.
    // CC=4  CogC=4  Nesting=2
    // -------------------------------------------------------------------------
    static void AlignObjectsToCoordinate(int axis, const AcGePoint3d& refPoint)
    {
        static const TCHAR* kAxisNames[] = { _T("X"), _T("Y"), _T("Z") };
        double coord = (axis == 0) ? refPoint.x : (axis == 1) ? refPoint.y : refPoint.z;

        acutPrintf(_T("\n=== ALIGN TO %s = %.3f ===\n"), kAxisNames[axis], coord);

        CommonTools::SelectionSetGuard ssGuard;
        if (!ssGuard.Get()) { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

        Adesk::Int32 length;
        acedSSLength(ssGuard.ss, &length);
        acutPrintf(_T("Selected %d objects. Aligning...\n"), length);

        int aligned = 0, skipped = 0;
        AcDbObjectIdArray processedGroups;
        auto groupMap = CommonTools::BuildEntityGroupMap(
            acdbHostApplicationServices()->workingDatabase());

        CommonTools::ForEachSsEntity(ssGuard.ss, length, [&](AcDbObjectId objId)
        {
            auto it = groupMap.find(objId);
            if (it != groupMap.end())
            {
                if (processedGroups.contains(it->second)) return;
                processedGroups.append(it->second);
                AlignGroup(it->second, axis, coord, 0, aligned);
            }
            else
            {
                AlignEntity(objId, axis, coord, 0, aligned, skipped);
            }
        });

        VerifySeqNumGroups();
        acutPrintf(_T("\nAlignment complete: %d aligned, %d skipped\n"), aligned, skipped);
    }

    // -------------------------------------------------------------------------
    // ALX / ALY / ALZ  — pick a reference point and align along that axis.
    // -------------------------------------------------------------------------
    void alignXCommand()
    {
        acutPrintf(_T("\n=== ALIGN OBJECTS TO X COORDINATE ===\n"));
        ads_point pt;
        if (acedGetPoint(NULL, _T("\nSelect reference point for X coordinate: "), pt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AlignObjectsToCoordinate(0, AcGePoint3d(pt[0], pt[1], pt[2]));
    }

    void alignYCommand()
    {
        acutPrintf(_T("\n=== ALIGN OBJECTS TO Y COORDINATE ===\n"));
        ads_point pt;
        if (acedGetPoint(NULL, _T("\nSelect reference point for Y coordinate: "), pt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AlignObjectsToCoordinate(1, AcGePoint3d(pt[0], pt[1], pt[2]));
    }

    void alignZCommand()
    {
        acutPrintf(_T("\n=== ALIGN OBJECTS TO Z COORDINATE ===\n"));
        ads_point pt;
        if (acedGetPoint(NULL, _T("\nSelect reference point for Z coordinate: "), pt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        AlignObjectsToCoordinate(2, AcGePoint3d(pt[0], pt[1], pt[2]));
    }

    // -------------------------------------------------------------------------
    // MoveAxisCommand: select objects and move along one axis.
    // Replaces the three identical moveX/Y/ZCommand bodies.
    // CC=3  CogC=3  Nesting=2
    // -------------------------------------------------------------------------
    static void MoveAxisCommand(int axis)
    {
        static const TCHAR* kNames[]   = { _T("X"), _T("Y"), _T("Z") };
        static const TCHAR* kPrompts[] = {
            _T("\nSpecify target point (only X distance will be used): "),
            _T("\nSpecify target point (only Y distance will be used): "),
            _T("\nSpecify target point (only Z distance will be used): ") };

        acutPrintf(_T("\n=== MOVE IN %s DIRECTION ONLY ===\n"), kNames[axis]);

        CommonTools::SelectionSetGuard ssGuard;
        if (!ssGuard.Get()) { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

        ads_point pt1;
        if (acedGetPoint(NULL, _T("\nSpecify base point: "), pt1) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        ads_point pt2;
        if (acedGetPoint(pt1, kPrompts[axis], pt2) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        double delta = pt2[axis] - pt1[axis];
        AcGeVector3d displacement(axis == 0 ? delta : 0, axis == 1 ? delta : 0, axis == 2 ? delta : 0);
        acutPrintf(_T("Moving %.2f units in %s direction\n"), delta, kNames[axis]);

        Adesk::Int32 length;
        acedSSLength(ssGuard.ss, &length);
        AcDbObjectIdArray processedGroups;
        auto groupMap = CommonTools::BuildEntityGroupMap(
            acdbHostApplicationServices()->workingDatabase());

        CommonTools::ForEachSsEntity(ssGuard.ss, length, [&](AcDbObjectId objId)
        {
            CommonTools::MoveEntityOrGroup(objId, displacement, processedGroups, groupMap);
        });

        acutPrintf(_T("Move complete!\n"));
    }

    void moveXCommand() { MoveAxisCommand(0); }
    void moveYCommand() { MoveAxisCommand(1); }
    void moveZCommand() { MoveAxisCommand(2); }

    // -------------------------------------------------------------------------
    // CopyObjectWithTransform: clone one entity (or its whole group) into
    // model space and apply transform. Group copies are not re-grouped.
    // -------------------------------------------------------------------------
    static void CopyObjectWithTransform(AcDbObjectId objId, const AcGeMatrix3d& transform,
                                        AcDbBlockTableRecord* pModelSpace,
                                        AcDbObjectIdArray& processedGroups,
                                        const CommonTools::EntityGroupMap& groupMap)
    {
        auto it = groupMap.find(objId);
        if (it != groupMap.end())
        {
            AcDbObjectId groupId = it->second;
            if (processedGroups.contains(groupId)) return;
            processedGroups.append(groupId);

            {
                CommonTools::AcDbObjectGuard<AcDbGroup> group(groupId);
                if (group)
                {
                    CommonTools::AcDbIteratorGuard<AcDbGroupIterator> iter(group->newIterator());
                    for (; !iter->done(); iter->next())
                    {
                        CommonTools::AcDbObjectGuard<AcDbEntity> ent(iter->objectId());
                        if (ent)
                        {
                            AcDbEntity* pCopy = AcDbEntity::cast(ent->clone());
                            if (pCopy)
                            { pCopy->transformBy(transform); pModelSpace->appendAcDbEntity(pCopy); pCopy->close(); }
                        }
                    }
                }
            }
        }
        else
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId);
            if (ent)
            {
                AcDbEntity* pCopy = AcDbEntity::cast(ent->clone());
                if (pCopy)
                { pCopy->transformBy(transform); pModelSpace->appendAcDbEntity(pCopy); pCopy->close(); }
            }
        }
    }

    // -------------------------------------------------------------------------
    // CopyAxisCommand: select objects and copy them along one axis.
    // Replaces the three identical copyX/Y/ZCommand bodies.
    // CC=4  CogC=4  Nesting=2
    // -------------------------------------------------------------------------
    static void CopyAxisCommand(int axis)
    {
        static const TCHAR* kNames[]   = { _T("X"), _T("Y"), _T("Z") };
        static const TCHAR* kPrompts[] = {
            _T("\nSpecify target point (only X distance will be used): "),
            _T("\nSpecify target point (only Y distance will be used): "),
            _T("\nSpecify target point (only Z distance will be used): ") };

        acutPrintf(_T("\n=== COPY IN %s DIRECTION ONLY ===\n"), kNames[axis]);

        CommonTools::SelectionSetGuard ssGuard;
        if (!ssGuard.Get()) { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

        ads_point pt1;
        if (acedGetPoint(NULL, _T("\nSpecify base point: "), pt1) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        ads_point pt2;
        if (acedGetPoint(pt1, kPrompts[axis], pt2) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        double delta = pt2[axis] - pt1[axis];
        AcGeVector3d displacement(axis == 0 ? delta : 0, axis == 1 ? delta : 0, axis == 2 ? delta : 0);
        AcGeMatrix3d transform = AcGeMatrix3d::translation(displacement);
        acutPrintf(_T("Copying %.2f units in %s direction\n"), delta, kNames[axis]);

        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (CommonTools::GetModelSpace(pModelSpace) != Acad::eOk)
        { acutPrintf(CommonTools::MSG_MODEL_SPACE_ERR); return; }

        Adesk::Int32 length;
        acedSSLength(ssGuard.ss, &length);
        AcDbObjectIdArray processedGroups;
        auto groupMap = CommonTools::BuildEntityGroupMap(
            acdbHostApplicationServices()->workingDatabase());

        CommonTools::ForEachSsEntity(ssGuard.ss, length, [&](AcDbObjectId objId)
        {
            CopyObjectWithTransform(objId, transform, pModelSpace, processedGroups, groupMap);
        });

        pModelSpace->close();
        acutPrintf(_T("Copy complete!\n"));
    }

    void copyXCommand() { CopyAxisCommand(0); }
    void copyYCommand() { CopyAxisCommand(1); }
    void copyZCommand() { CopyAxisCommand(2); }

    // -------------------------------------------------------------------------
    // PLACEMID command - Move an object to the midpoint between two points.
    // -------------------------------------------------------------------------
    void placeMidCommand()
    {
        acutPrintf(_T("\n=== PLACE AT MIDPOINT ===\n"));

        ads_name ent;
        ads_point pickPt;
        if (acedEntSel(_T("\nSelect object to place: "), ent, pickPt) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        AcDbObjectId objId;
        acdbGetObjectId(objId, ent);

        // Use bounding-box center as the object's reference point.
        CommonTools::AcDbObjectGuard<AcDbEntity> entGuard(objId);
        if (!entGuard) { acutPrintf(_T("\nError: Could not open object.\n")); return; }
        AcDbExtents extents;
        AcGePoint3d objCenter;
        if (entGuard->getGeomExtents(extents) == Acad::eOk)
            objCenter = extents.minPoint() + (extents.maxPoint() - extents.minPoint()) * 0.5;
        else
            objCenter = AcGePoint3d(pickPt[0], pickPt[1], pickPt[2]);

        ads_point pt1, pt2;
        if (acedGetPoint(NULL, _T("\nPick first reference point: "), pt1) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }
        if (acedGetPoint(pt1, _T("\nPick second reference point: "), pt2) != RTNORM)
        { acutPrintf(CommonTools::MSG_CANCELLED); return; }

        AcGePoint3d midPoint((pt1[0] + pt2[0]) * 0.5,
                             (pt1[1] + pt2[1]) * 0.5,
                             (pt1[2] + pt2[2]) * 0.5);
        AcGeVector3d displacement = midPoint - objCenter;

        AcDbObjectIdArray processedGroups;
        CommonTools::MoveEntityOrGroup(objId, displacement, processedGroups);

        acutPrintf(_T("\nObject placed at midpoint (%.2f, %.2f, %.2f)\n"),
                   midPoint.x, midPoint.y, midPoint.z);
    }

} // namespace AlignTools
