// PolylineTools.cpp - Polyline and Boolean Operations Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "PolylineTools.h"
#include "CommonTools.h"
#include "dbregion.h"

namespace PolylineTools
{
    // Helper function: Get region from polyline
    // Region from one closed curve (polyline, circle, ellipse, closed spline),
    // as AutoCAD's REGION command builds it.
    static AcDbRegion* CreateRegionFromCurve(AcDbCurve* pCurve)
    {
        if (!pCurve)
            return nullptr;

        AcDbVoidPtrArray curves;
        curves.append(pCurve);

        AcDbVoidPtrArray regions;
        Acad::ErrorStatus es = AcDbRegion::createFromCurves(curves, regions);

        if (es != Acad::eOk || regions.length() == 0)
            return nullptr;

        return static_cast<AcDbRegion*>(regions[0]);
    }

    AcDbObjectId BooleanPolylines(AcDbObjectId first, AcDbObjectId second,
                                  AcDb::BoolOperType op, CString* err)
    {
        auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

        AcDbRegion* pRegion1 = nullptr;
        AcDbRegion* pRegion2 = nullptr;
        {
            CommonTools::AcDbObjectGuard<AcDbCurve> curve1(first);
            CommonTools::AcDbObjectGuard<AcDbCurve> curve2(second);
            if (!curve1 || !curve2)
                return fail(_T("both objects must be closed curves (polyline, circle, ellipse, closed spline)"));
            if (!curve1->isClosed() || !curve2->isClosed())
                return fail(_T("both curves must be closed"));
            pRegion1 = CreateRegionFromCurve(curve1.get());
            pRegion2 = CreateRegionFromCurve(curve2.get());
        }

        if (!pRegion1 || !pRegion2)
        {
            delete pRegion1;
            delete pRegion2;
            return fail(_T("could not create regions from the curves (self-intersecting?)"));
        }

        Acad::ErrorStatus es = pRegion1->booleanOper(op, pRegion2);
        delete pRegion2;
        if (es != Acad::eOk)
        { delete pRegion1; return fail(_T("boolean operation failed")); }

        // An empty result (disjoint shapes for intersect, the second covering
        // the first for subtract) is a failure, not an invisible region.
        double area = 0.0;
        if (pRegion1->isNull() || pRegion1->getArea(area) != Acad::eOk || area <= 1e-9)
        {
            delete pRegion1;
            return fail(op == AcDb::kBoolIntersect ? _T("empty result: the shapes do not overlap")
                                                   : _T("empty result: nothing is left"));
        }

        pRegion1->setColorIndex(3); // Green
        AcDbObjectId resultId = CommonTools::AppendToModelSpace(pRegion1);
        if (resultId.isNull()) return fail(_T("could not add result to drawing"));
        return resultId;
    }

    // -------------------------------------------------------------------------
    // OrderCurveSegments: sort exploded region curves into a connected chain.
    // Returns false if a gap is found (partial ordering preserved).
    // -------------------------------------------------------------------------
    static bool OrderCurveSegments(const AcDbVoidPtrArray& curves, int totalSegments,
                                   AcArray<int>& orderedIndices, AcArray<bool>& reversed)
    {
        AcArray<bool> used;
        used.setLogicalLength(totalSegments);
        for (int i = 0; i < totalSegments; i++) used[i] = false;

        orderedIndices.append(0);
        reversed.append(false);
        used[0] = true;

        AcDbCurve* pFirstCurve = AcDbCurve::cast(static_cast<AcDbEntity*>(curves[0]));
        AcGePoint3d currentEnd;
        if (pFirstCurve) pFirstCurve->getEndPoint(currentEnd);

        const double tolerance = 0.001;

        for (int found = 1; found < totalSegments; found++)
        {
            bool foundNext = false;
            for (int i = 0; i < totalSegments; i++)
            {
                if (used[i]) continue;
                AcDbCurve* pCurve = AcDbCurve::cast(static_cast<AcDbEntity*>(curves[i]));
                if (!pCurve) continue;

                AcGePoint3d start, end;
                pCurve->getStartPoint(start);
                pCurve->getEndPoint(end);

                if (currentEnd.distanceTo(start) < tolerance)
                { orderedIndices.append(i); reversed.append(false); used[i] = true; currentEnd = end;   foundNext = true; break; }
                else if (currentEnd.distanceTo(end) < tolerance)
                { orderedIndices.append(i); reversed.append(true);  used[i] = true; currentEnd = start; foundNext = true; break; }
            }
            if (!foundNext)
            { acutPrintf(_T("\nWarning: Could not find connected segment at position %d\n"), found); return false; }
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // BuildPolylineFromCurves: construct an AcDbPolyline from an ordered curve
    // chain. Caller is responsible for deleting the returned object on failure.
    // -------------------------------------------------------------------------
    static AcDbPolyline* BuildPolylineFromCurves(const AcDbVoidPtrArray& curves,
                                                  const AcArray<int>& orderedIndices,
                                                  const AcArray<bool>& reversed)
    {
        AcDbPolyline* pPoly = new AcDbPolyline();

        for (int i = 0; i < orderedIndices.length(); i++)
        {
            int idx = orderedIndices[i];
            bool rev = reversed[i];
            AcDbEntity* pEnt   = static_cast<AcDbEntity*>(curves[idx]);
            AcDbCurve*  pCurve = AcDbCurve::cast(pEnt);
            if (!pCurve) continue;

            AcGePoint3d start, end;
            pCurve->getStartPoint(start);
            pCurve->getEndPoint(end);
            AcGePoint3d vertexPt = rev ? end : start;

            double bulge = 0.0;
            if (pEnt->isKindOf(AcDbArc::desc()))
            {
                AcDbArc* pArc = static_cast<AcDbArc*>(pEnt);
                double includedAngle = pArc->endAngle() - pArc->startAngle();
                if (includedAngle < 0.0) includedAngle += 2.0 * M_PI;
                bulge = tan(includedAngle / 4.0);
                if (rev) bulge = -bulge;
            }

            pPoly->addVertexAt(i, AcGePoint2d(vertexPt.x, vertexPt.y), bulge);
        }

        return pPoly;
    }

    AcDbObjectId RegionToPolyline(AcDbObjectId regionId, CString* err)
    {
        auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

        AcDbVoidPtrArray curves;
        {
            CommonTools::AcDbObjectGuard<AcDbRegion> region(regionId);
            if (!region)
                return fail(_T("object is not a region"));
            if (region->explode(curves) != Acad::eOk || curves.length() == 0)
                return fail(_T("could not explode region"));
        }

        int totalSegments = curves.length();
        AcArray<int>  orderedIndices;
        AcArray<bool> reversed;
        OrderCurveSegments(curves, totalSegments, orderedIndices, reversed);

        AcDbPolyline* pPoly = BuildPolylineFromCurves(curves, orderedIndices, reversed);

        for (int i = 0; i < totalSegments; i++)
            delete static_cast<AcDbEntity*>(curves[i]);

        pPoly->setClosed(Adesk::kTrue);
        pPoly->setColorIndex(3); // Green

        AcDbObjectId polyId = CommonTools::AppendToModelSpace(pPoly);
        if (polyId.isNull()) return fail(_T("could not add polyline to drawing"));
        return polyId;
    }
}
