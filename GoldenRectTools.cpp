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

    using CommonTools::AppendToModelSpace;

    AcDbObjectId DrawPolyRect(const AcGePoint3d pts[4])
    {
        AcDbPolyline* pPl = new AcDbPolyline();
        for (int i = 0; i < 4; ++i)
            pPl->addVertexAt(i, AcGePoint2d(pts[i].x, pts[i].y));
        pPl->setClosed(true);
        return AppendToModelSpace(pPl);
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

bool GoldenRectTools::DrawGoldenSpiral(const AcGePoint3d& corner, const AcGePoint3d& sidePt)
{
    AcGeVector3d dir(sidePt.x - corner.x, sidePt.y - corner.y, 0.0);
    double sideLen = dir.length();
    if (sideLen < 0.001) return false;
    dir /= sideLen;

    AcGeVector3d perp(-dir.y, dir.x, 0.0);
    AcGePoint3d origin(corner.x, corner.y, 0.0);

    DrawSpiralRect(0, origin, dir, perp, sideLen, sideLen / kGoldenRatio);
    return true;
}

// ── GOLDEN RECT IN / INW (non-interactive cores) ─────────────────────────────

bool GoldenRectTools::ReadRectFrame(const AcDbObjectId& id, RectFrame& frame, CString& err)
{
    AcGePoint3d corners[4];
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> pline(id);
        if (!pline)
        { err = _T("Selected object is not a polyline."); return false; }

        if (!pline->isClosed())
        { err = _T("Polyline must be closed (rectangle expected)."); return false; }

        unsigned int nVerts = pline->numVerts();
        if (nVerts != 4)
        { err.Format(_T("Expected 4 vertices for a rectangle, got %u."), nVerts); return false; }

        for (unsigned int i = 0; i < 4; ++i)
            pline->getPointAt(i, corners[i]);
    }

    AcGeVector3d dW = corners[1] - corners[0];
    AcGeVector3d dH = corners[3] - corners[0];
    double W = dW.length();
    double H = dH.length();

    if (W < 0.001 || H < 0.001)
    { err = _T("Rectangle is too small."); return false; }

    AcGeVector3d dirW = dW / W;
    AcGeVector3d dirH = dH / H;

    if (fabs(dirW.dotProduct(dirH)) > 0.001)
    { err = _T("Polyline edges are not perpendicular (not a rectangle)."); return false; }

    frame.origin = corners[0];
    frame.width  = W;
    frame.height = H;
    if (W <= H)
    {
        frame.shortLen = W;  frame.longLen = H;
        frame.shortVec = dW; frame.longDir = dirH;
    }
    else
    {
        frame.shortLen = H;  frame.longLen = W;
        frame.shortVec = dH; frame.longDir = dirW;
    }
    return true;
}

AcDbObjectId GoldenRectTools::DrawRectInFrame(const RectFrame& frame, double innerWidth,
                                              const AcGePoint3d& pick)
{
    const AcGePoint3d& O = frame.origin;
    double t = (pick - O).dotProduct(frame.longDir);

    double halfWidth = innerWidth * 0.5;
    if (t < halfWidth)
        t = halfWidth;
    else if (t > frame.longLen - halfWidth)
        t = frame.longLen - halfWidth;

    double offset = t - halfWidth;

    AcGePoint3d grPts[4];
    grPts[0] = O + frame.longDir * offset;
    grPts[1] = O + frame.longDir * offset + frame.shortVec;
    grPts[2] = O + frame.longDir * offset + frame.shortVec + frame.longDir * innerWidth;
    grPts[3] = O + frame.longDir * offset + frame.longDir * innerWidth;

    return DrawPolyRect(grPts);
}
