// PolylineTools.cpp - Polyline and Boolean Operations Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "PolylineTools.h"
#include "CommonTools.h"
#include "dbregion.h"

namespace PolylineTools
{
    // Helper function: Get region from polyline
    static AcDbRegion* CreateRegionFromPolyline(AcDbPolyline* pPoly)
    {
        if (!pPoly)
            return nullptr;

        AcDbVoidPtrArray curves;
        curves.append(pPoly);

        AcDbVoidPtrArray regions;
        Acad::ErrorStatus es = AcDbRegion::createFromCurves(curves, regions);

        if (es != Acad::eOk || regions.length() == 0)
            return nullptr;

        return static_cast<AcDbRegion*>(regions[0]);
    }

    // Helper function for boolean operations
    // CC=4  CogC=4  Nesting=2
    static Acad::ErrorStatus PerformBooleanOperation(
        AcDb::BoolOperType operation,
        const TCHAR* operationName)
    {
        ads_name ename1; ads_point pt1;
        if (acedEntSel(_T("\nSelect first polyline: "), ename1, pt1) != RTNORM)
        { acutPrintf(_T("\nSelection cancelled.\n")); return Acad::eInvalidInput; }

        AcDbObjectId objId1;
        acdbGetObjectId(objId1, ename1);
        if (!CommonTools::AcDbObjectGuard<AcDbPolyline>(objId1))
        { acutPrintf(_T("\nError: Selected object is not a polyline.\n")); return Acad::eInvalidInput; }

        ads_name ename2; ads_point pt2;
        if (acedEntSel(_T("\nSelect second polyline: "), ename2, pt2) != RTNORM)
        { acutPrintf(_T("\nSelection cancelled.\n")); return Acad::eInvalidInput; }

        AcDbObjectId objId2;
        acdbGetObjectId(objId2, ename2);

        CString err;
        if (BooleanPolylines(objId1, objId2, operation, &err).isNull())
        {
            acutPrintf(_T("\nError: %s (%s).\n"), (LPCTSTR)err, operationName);
            return Acad::eInvalidInput;
        }
        acutPrintf(_T("\n%s operation completed successfully!\n"), operationName);
        return Acad::eOk;
    }

    AcDbObjectId BooleanPolylines(AcDbObjectId first, AcDbObjectId second,
                                  AcDb::BoolOperType op, CString* err)
    {
        auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

        AcDbRegion* pRegion1 = nullptr;
        AcDbRegion* pRegion2 = nullptr;
        {
            CommonTools::AcDbObjectGuard<AcDbPolyline> poly1(first);
            CommonTools::AcDbObjectGuard<AcDbPolyline> poly2(second);
            if (!poly1 || !poly2)
                return fail(_T("both objects must be polylines"));
            pRegion1 = CreateRegionFromPolyline(poly1.get());
            pRegion2 = CreateRegionFromPolyline(poly2.get());
        }

        if (!pRegion1 || !pRegion2)
        {
            delete pRegion1;
            delete pRegion2;
            return fail(_T("could not create regions from polylines (are they closed?)"));
        }

        Acad::ErrorStatus es = pRegion1->booleanOper(op, pRegion2);
        delete pRegion2;
        if (es != Acad::eOk)
        { delete pRegion1; return fail(_T("boolean operation failed")); }

        pRegion1->setColorIndex(3); // Green
        AcDbObjectId resultId = CommonTools::AppendToModelSpace(pRegion1);
        if (resultId.isNull()) return fail(_T("could not add result to drawing"));
        return resultId;
    }

    // SUBPOLY command - Subtract second polyline from first
    void subtractPolyCommand()
    {
        acutPrintf(_T("\n=== SUBTRACT POLYLINES (1st - 2nd) ===\n"));
        PerformBooleanOperation(AcDb::kBoolSubtract, _T("Subtract"));
    }

    // INPOLY command - Intersection of two polylines
    void intersectPolyCommand()
    {
        acutPrintf(_T("\n=== INTERSECT POLYLINES ===\n"));
        PerformBooleanOperation(AcDb::kBoolIntersect, _T("Intersection"));
    }

    // UNIONPOLY command - Union of two polylines
    void unionPolyCommand()
    {
        acutPrintf(_T("\n=== UNION POLYLINES ===\n"));
        PerformBooleanOperation(AcDb::kBoolUnite, _T("Union"));
    }

    // BOOLPOLY command - Interactive boolean: prompt for op then delegate.
    // CC=4  CogC=3  Nesting=1
    void booleanPolyCommand()
    {
        acutPrintf(_T("\n==========================================\n"));
        acutPrintf(_T("   Boolean Operations on Polylines       \n"));
        acutPrintf(_T("==========================================\n"));
        acutPrintf(_T("  U = Union\n  I = Intersection\n  S = Subtract (1st - 2nd)\n"));

        TCHAR opStr[10];
        if (acedGetString(0, _T("\nEnter operation [U/I/S]: "), opStr) != RTNORM)
        { acutPrintf(_T("\nCommand cancelled.\n")); return; }

        TCHAR op = _totupper(opStr[0]);
        if      (op == _T('U')) PerformBooleanOperation(AcDb::kBoolUnite,     _T("Union"));
        else if (op == _T('I')) PerformBooleanOperation(AcDb::kBoolIntersect, _T("Intersection"));
        else if (op == _T('S')) PerformBooleanOperation(AcDb::kBoolSubtract,  _T("Subtract"));
        else    acutPrintf(_T("\nInvalid operation. Use U, I, or S.\n"));
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

    // REG2POLY command - Convert region to polyline with ordered segments.
    // CC=4  CogC=4  Nesting=2
    void regionToPolyCommand()
    {
        acutPrintf(_T("\n=== CONVERT REGION TO POLYLINE ===\n"));

        ads_name ename; ads_point pt;
        if (acedEntSel(_T("\nSelect region: "), ename, pt) != RTNORM)
        { acutPrintf(_T("\nSelection cancelled.\n")); return; }

        AcDbObjectId objId;
        acdbGetObjectId(objId, ename);

        CString err;
        AcDbObjectId polyId = RegionToPolyline(objId, &err);
        if (polyId.isNull())
        { acutPrintf(_T("\nError: %s.\n"), (LPCTSTR)err); return; }

        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(polyId);
        acutPrintf(_T("\nPolyline created with %d vertices!\n"), poly ? (int)poly->numVerts() : 0);
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
