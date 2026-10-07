// AlignTools.cpp - Object Alignment Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
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
    // AlignGroup: move a whole group so the center of its first circle sits at
    // coord (SEQNUM groups: number + circle). Groups without a circle are skipped.
    // -------------------------------------------------------------------------
    static bool AlignGroup(const CommonTools::GroupUnit& u, int axis, double coord)
    {
        for (AcDbObjectId id : u.members)
        {
            AcGePoint3d center;
            {
                CommonTools::AcDbObjectGuard<AcDbCircle> circle(id);
                if (!circle) continue;
                center = circle->center();
            }
            CommonTools::TranslateEntities(u.members, AxisDelta(axis, coord, center));
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // AlignEntity: align a single entity (not in a group) along one axis.
    // Dispatches by entity type; falls back to bounding-box min-point.
    // -------------------------------------------------------------------------
    static bool AlignEntity(AcDbObjectId objId, int axis, double coord)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
        if (!ent) return false;
        AcDbEntity* pEnt = ent.get();

        if (pEnt->isKindOf(AcDbCircle::desc()))
        {
            AcDbCircle* p = static_cast<AcDbCircle*>(pEnt);
            AcGePoint3d c = p->center();
            SetAxisCoord(c, axis, coord);
            p->setCenter(c);
            return true;
        }
        if (pEnt->isKindOf(AcDbArc::desc()))
        {
            AcDbArc* p = static_cast<AcDbArc*>(pEnt);
            AcGePoint3d c = p->center();
            SetAxisCoord(c, axis, coord);
            p->setCenter(c);
            return true;
        }
        if (pEnt->isKindOf(AcDbCurve::desc()))
        {
            AcGePoint3d start;
            if (static_cast<AcDbCurve*>(pEnt)->getStartPoint(start) != Acad::eOk) return false;
            pEnt->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, start)));
            return true;
        }
        if (pEnt->isKindOf(AcDbText::desc()))
        {
            AcDbText* p = static_cast<AcDbText*>(pEnt);
            AcGePoint3d pos = p->position();
            SetAxisCoord(pos, axis, coord);
            p->setPosition(pos);
            return true;
        }
        if (pEnt->isKindOf(AcDbMText::desc()))
        {
            AcDbMText* p = static_cast<AcDbMText*>(pEnt);
            AcGePoint3d loc = p->location();
            SetAxisCoord(loc, axis, coord);
            p->setLocation(loc);
            return true;
        }
        if (pEnt->isKindOf(AcDbBlockReference::desc()))
        {
            AcDbBlockReference* p = static_cast<AcDbBlockReference*>(pEnt);
            AcGePoint3d pos = p->position();
            SetAxisCoord(pos, axis, coord);
            p->setPosition(pos);
            return true;
        }
        if (pEnt->isKindOf(AcDbRegion::desc()))
        {
            AcDbVoidPtrArray curves;
            if (static_cast<AcDbRegion*>(pEnt)->explode(curves) != Acad::eOk) return false;
            bool modified = false;
            if (curves.length() > 0)
            {
                AcDbCurve* pFirst = AcDbCurve::cast(static_cast<AcDbEntity*>(curves[0]));
                AcGePoint3d refPt;
                if (pFirst && pFirst->getStartPoint(refPt) == Acad::eOk)
                {
                    pEnt->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, refPt)));
                    modified = true;
                }
            }
            for (int j = 0; j < curves.length(); j++)
                delete static_cast<AcDbEntity*>(curves[j]);
            return modified;
        }

        AcDbExtents ext;
        if (pEnt->getGeomExtents(ext) != Acad::eOk) return false;
        pEnt->transformBy(AcGeMatrix3d::translation(AxisDelta(axis, coord, ext.minPoint())));
        return true;
    }

    int AlignObjects(const std::vector<AcDbObjectId>& ids, int axis, double coord)
    {
        int aligned = 0;
        for (const CommonTools::GroupUnit& u : CommonTools::GroupUnits(ids))
        {
            bool ok = u.groupId.isNull() ? AlignEntity(u.picked, axis, coord)
                                         : AlignGroup(u, axis, coord);
            if (ok) aligned++;
        }
        return aligned;
    }

    std::vector<AcDbObjectId> CopyObjects(const std::vector<AcDbObjectId>& ids, const AcGeVector3d& delta)
    {
        std::vector<AcDbObjectId> newIds;
        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (CommonTools::GetModelSpace(pModelSpace) != Acad::eOk) return newIds;

        AcGeMatrix3d transform = AcGeMatrix3d::translation(delta);
        for (const CommonTools::GroupUnit& u : CommonTools::GroupUnits(ids))
        {
            for (AcDbObjectId id : u.members)
            {
                CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
                if (!ent) continue;
                AcDbEntity* pCopy = AcDbEntity::cast(ent->clone());
                if (!pCopy) continue;
                pCopy->transformBy(transform);
                AcDbObjectId newId = CommonTools::AppendEntity(pModelSpace, pCopy);
                if (!newId.isNull()) newIds.push_back(newId);
            }
        }

        pModelSpace->close();
        return newIds;
    }

} // namespace AlignTools
