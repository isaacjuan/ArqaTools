#include "StdAfx.h"
#include "GoldenRectTools.h"
#include "CommonTools.h"
#include "dbents.h"
#include "dbpl.h"
#include "geassign.h"

namespace
{
    constexpr double kGoldenRatio = 1.618033988749895;
    constexpr int    kRecurrences = 6;

    void AppendToModelSpace(AcDbEntity* pEnt)
    {
        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (CommonTools::GetModelSpace(pModelSpace) != Acad::eOk)
        { delete pEnt; return; }
        pModelSpace->appendAcDbEntity(pEnt);
        pEnt->close();
        pModelSpace->close();
    }

    void DrawPolyRect(const AcGePoint3d pts[4])
    {
        AcDbPolyline* pPl = new AcDbPolyline();
        for (int i = 0; i < 4; ++i)
            pPl->addVertexAt(i, AcGePoint2d(pts[i].x, pts[i].y));
        pPl->setClosed(true);
        AppendToModelSpace(pPl);
    }

    bool PickPoint(const AcGePoint3d* basePt, const TCHAR* prompt, AcGePoint3d& out)
    {
        ads_point pt;
        int result;
        if (basePt)
        {
            ads_point bp;
            bp[0] = basePt->x; bp[1] = basePt->y; bp[2] = basePt->z;
            result = acedGetPoint(bp, prompt, pt);
        }
        else
        {
            result = acedGetPoint(NULL, prompt, pt);
        }
        if (result != RTNORM) return false;
        out.x = pt[0]; out.y = pt[1]; out.z = pt[2];
        return true;
    }
}

// ── GOLDEN RECT (spiral) ──────────────────────────────────────────────────────

namespace
{
    void DrawSpiralRect(int level, const AcGePoint3d& origin,
                         const AcGeVector3d& dW, const AcGeVector3d& dH,
                         double w, double h)
    {
        if (level >= kRecurrences)
            return;

        AcGePoint3d p0(origin.x, origin.y, 0.0);
        AcGePoint3d p1(origin.x + dW.x * w, origin.y + dW.y * w, 0.0);

        if (level == 0)
        {
            AcGePoint3d p2(origin.x + dW.x * w + dH.x * h,
                           origin.y + dW.y * w + dH.y * h, 0.0);
            AcGePoint3d p3(origin.x + dH.x * h, origin.y + dH.y * h, 0.0);

            AcDbPolyline* pPl = new AcDbPolyline();
            pPl->addVertexAt(0, AcGePoint2d(p0.x, p0.y));
            pPl->addVertexAt(1, AcGePoint2d(p1.x, p1.y));
            pPl->addVertexAt(2, AcGePoint2d(p2.x, p2.y));
            pPl->addVertexAt(3, AcGePoint2d(p3.x, p3.y));
            pPl->setClosed(true);
            AppendToModelSpace(pPl);
        }
        else
        {
            AcDbLine* pLn = new AcDbLine(p0, p1);
            AppendToModelSpace(pLn);
        }

        AcGePoint3d nextOrigin(origin.x + dW.x * h + dH.x * h,
                               origin.y + dW.y * h + dH.y * h, 0.0);
        AcGeVector3d nextDW(-dH.x, -dH.y, 0.0);
        AcGeVector3d nextDH(dW.x, dW.y, 0.0);

        DrawSpiralRect(level + 1, nextOrigin, nextDW, nextDH, h, w - h);
    }
}

void GoldenRectTools::goldenRectCommand()
{
    AcGePoint3d corner;
    if (!PickPoint(nullptr, _T("\nGolden rectangle start corner: "), corner))
    { acutPrintf(_T("\nCancelled.\n")); return; }

    AcGePoint3d sidePt;
    if (!PickPoint(&corner, _T("\nEndpoint defining first side: "), sidePt))
    { acutPrintf(_T("\nCancelled.\n")); return; }

    AcGeVector3d dir(sidePt.x - corner.x, sidePt.y - corner.y, 0.0);
    double sideLen = dir.length();
    if (sideLen < 0.001) { acutPrintf(_T("\nPoints are too close.\n")); return; }
    dir /= sideLen;

    AcGeVector3d perp(-dir.y, dir.x, 0.0);
    AcGePoint3d origin(corner.x, corner.y, 0.0);

    DrawSpiralRect(0, origin, dir, perp, sideLen, sideLen / kGoldenRatio);

    acutPrintf(_T("\n%d golden rectangles drawn, spiraling inward.\n"), kRecurrences);
}

// ── GOLDEN RECT IN / INW (common implementation) ──────────────────────────────

namespace
{
    void PlaceRectsInContainer(bool goldenProportion)
    {
        ads_name ss;
        TCHAR prompt[] = _T("\nSelect containing rectangle: ");
        if (acedSSGet(_T(":S"), NULL, NULL, NULL, ss) != RTNORM)
        { acutPrintf(_T("\nNothing selected.\n")); return; }

        Adesk::Int32 len = 0;
        acedSSLength(ss, &len);
        if (len == 0) { acedSSFree(ss); acutPrintf(_T("\nNothing selected.\n")); return; }

        ads_name ename;
        acedSSName(ss, 0, ename);
        acedSSFree(ss);

        AcDbObjectId objId;
        AcDbPolyline* pPline = nullptr;
        if (acdbGetObjectId(objId, ename) != Acad::eOk ||
            acdbOpenObject(pPline, objId, AcDb::kForRead) != Acad::eOk)
        { acutPrintf(_T("\nSelected object is not a polyline.\n")); return; }

        if (!pPline->isClosed())
        { pPline->close(); acutPrintf(_T("\nPolyline must be closed (rectangle expected).\n")); return; }

        unsigned int nVerts = pPline->numVerts();
        if (nVerts != 4)
        {
            TCHAR buf[64];
            _stprintf_s(buf, _T("Expected 4 vertices for a rectangle, got %u.\n"), nVerts);
            pPline->close();
            acutPrintf(buf);
            return;
        }

        AcGePoint3d corners[4];
        for (unsigned int i = 0; i < 4; ++i)
            pPline->getPointAt(i, corners[i]);
        pPline->close();

        AcGeVector3d dW = corners[1] - corners[0];
        AcGeVector3d dH = corners[3] - corners[0];
        double W = dW.length();
        double H = dH.length();

        if (W < 0.001 || H < 0.001)
        { acutPrintf(_T("\nRectangle is too small.\n")); return; }

        AcGeVector3d dirW = dW / W;
        AcGeVector3d dirH = dH / H;

        double dot = fabs(dirW.dotProduct(dirH));
        if (dot > 0.001)
        { acutPrintf(_T("\nPolyline edges are not perpendicular (not a rectangle).\n")); return; }

        double shortLen, longLen;
        AcGeVector3d shortVec, longVec;
        AcGeVector3d shortDir, longDir;
        if (W <= H)
        {
            shortLen = W;  longLen = H;
            shortDir = dirW; longDir = dirH;
            shortVec = dW;  longVec = dH;
        }
        else
        {
            shortLen = H;  longLen = W;
            shortDir = dirH; longDir = dirW;
            shortVec = dH;  longVec = dW;
        }

        double innerWidth;
        double proportion = 0.0;
        if (goldenProportion)
        {
            innerWidth = shortLen / kGoldenRatio;
        }
        else
        {
            TCHAR propBuf[32] = {};
            if (acedGetString(Adesk::kFalse, _T("\nProportion (0-1, e.g. 1=square, 0.5=half height): "),
                              propBuf) != RTNORM)
            { acutPrintf(_T("\nCancelled.\n")); return; }
            proportion = _tstof(propBuf);
            if (proportion <= 0.0 || proportion > 1.0)
            { acutPrintf(_T("\nProportion must be between 0 and 1.\n")); return; }
            innerWidth = shortLen * proportion;
        }

        AcGePoint3d O = corners[0];

        int count = 0;
        for (;;)
        {
            AcGePoint3d pickPt;
            if (!PickPoint(nullptr, goldenProportion
                          ? _T("\nPick location for golden rectangle: ")
                          : _T("\nPick location for inner rectangle: "), pickPt))
                break;

            AcGeVector3d fromOrigin = pickPt - O;
            double t = fromOrigin.dotProduct(longDir);

            double halfWidth = innerWidth * 0.5;
            if (t < halfWidth)
                t = halfWidth;
            else if (t > longLen - halfWidth)
                t = longLen - halfWidth;

            double offset = t - halfWidth;

            AcGePoint3d grPts[4];
            grPts[0] = O + longDir * offset;
            grPts[1] = O + longDir * offset + shortVec;
            grPts[2] = O + longDir * offset + shortVec + longDir * innerWidth;
            grPts[3] = O + longDir * offset + longDir * innerWidth;

            DrawPolyRect(grPts);
            ++count;
        }

        if (goldenProportion)
            acutPrintf(_T("\n%d golden rectangle(s) (%.2f x %.2f) drawn inside %.0f x %.0f container.\n"),
                       count, shortLen, innerWidth, W, H);
        else
            acutPrintf(_T("\n%d rectangle(s) (%.2f x %.2f, proportion %.3f) drawn inside %.0f x %.0f container.\n"),
                       count, shortLen, innerWidth, proportion, W, H);
    }
}

void GoldenRectTools::goldenRectInCommand()
{
    PlaceRectsInContainer(true);
}

void GoldenRectTools::goldenRectInWCommand()
{
    PlaceRectsInContainer(false);
}
