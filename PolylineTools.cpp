// PolylineTools.cpp - Polyline and Boolean Operations Implementation

#include "StdAfx.h"
#include "HelloWorld.h"
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
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly1(objId1);
        if (!poly1)
        { acutPrintf(_T("\nError: Selected object is not a polyline.\n")); return Acad::eInvalidInput; }

        ads_name ename2; ads_point pt2;
        if (acedEntSel(_T("\nSelect second polyline: "), ename2, pt2) != RTNORM)
        { acutPrintf(_T("\nSelection cancelled.\n")); return Acad::eInvalidInput; }

        AcDbObjectId objId2;
        acdbGetObjectId(objId2, ename2);
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly2(objId2);
        if (!poly2)
        { acutPrintf(_T("\nError: Selected object is not a polyline.\n")); return Acad::eInvalidInput; }

        AcDbRegion* pRegion1 = CreateRegionFromPolyline(poly1.get());
        AcDbRegion* pRegion2 = CreateRegionFromPolyline(poly2.get());

        if (!pRegion1 || !pRegion2)
        {
            if (pRegion1) delete pRegion1;
            if (pRegion2) delete pRegion2;
            acutPrintf(_T("\nError: Could not create regions from polylines.\n"));
            return Acad::eInvalidInput;
        }

        Acad::ErrorStatus es = pRegion1->booleanOper(operation, pRegion2);
        delete pRegion2;

        if (es != Acad::eOk)
        { delete pRegion1; acutPrintf(_T("\nError: %s operation failed.\n"), operationName); return es; }

        AcDbBlockTableRecord* pModelSpace = nullptr;
        es = CommonTools::GetModelSpace(pModelSpace);
        if (es == Acad::eOk)
        {
            pRegion1->setColorIndex(3); // Green
            pModelSpace->appendAcDbEntity(pRegion1);
            pModelSpace->close();
            pRegion1->close();
            acutPrintf(_T("\n%s operation completed successfully!\n"), operationName);
            return Acad::eOk;
        }
        delete pRegion1;
        acutPrintf(_T("\nError: Could not add result to drawing.\n"));
        return es;
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
        CommonTools::AcDbObjectGuard<AcDbRegion> region(objId);
        if (!region)
        { acutPrintf(_T("\nError: Selected object is not a region.\n")); return; }

        AcDbVoidPtrArray curves;
        Acad::ErrorStatus es = region->explode(curves);

        if (es != Acad::eOk || curves.length() == 0)
        { acutPrintf(_T("\nError: Could not explode region.\n")); return; }

        int totalSegments = curves.length();
        acutPrintf(_T("\nExploded into %d segments. Ordering segments...\n"), totalSegments);

        AcArray<int>  orderedIndices;
        AcArray<bool> reversed;
        OrderCurveSegments(curves, totalSegments, orderedIndices, reversed);
        acutPrintf(_T("Ordered %d segments successfully\n"), orderedIndices.length());

        AcDbPolyline* pPoly = BuildPolylineFromCurves(curves, orderedIndices, reversed);

        for (int i = 0; i < totalSegments; i++)
            delete static_cast<AcDbEntity*>(curves[i]);

        pPoly->setClosed(Adesk::kTrue);
        pPoly->setColorIndex(3); // Green

        AcDbBlockTableRecord* pModelSpace = nullptr;
        es = CommonTools::GetModelSpace(pModelSpace);
        if (es == Acad::eOk)
        {
            pModelSpace->appendAcDbEntity(pPoly);
            pModelSpace->close();
            pPoly->close();
            acutPrintf(_T("\nPolyline created with %d vertices!\n"), orderedIndices.length());
        }
        else
        {
            delete pPoly;
            acutPrintf(_T("\nError: Could not add polyline to drawing.\n"));
        }
    }
}
