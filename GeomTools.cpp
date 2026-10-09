#include "StdAfx.h"
#include "GeomTools.h"
#include "CommonTools.h"
#include "gecomp3d.h"
#include "gearc3d.h"
#include "gelnsg3d.h"
#include "geintrvl.h"
#include <cmath>

namespace GeomTools
{
namespace
{
    const double kPi = 3.14159265358979323846;

    // Segments for an arc of `sweep` radians: one per 5 degrees, at least 4.
    int ArcSegments(double sweep)
    {
        int n = static_cast<int>(std::ceil(std::fabs(sweep) / (5.0 * kPi / 180.0)));
        return n < 4 ? 4 : n;
    }

    void Add(Outline& poly, const AcGePoint3d& p)
    {
        AcGePoint2d q(p.x, p.y);
        if (poly.empty() || !poly.back().isEqualTo(q))
            poly.push_back(q);
    }

    // Appends the curve's points from its start up to (not including) its end.
    void AppendGeCurve(const AcGeCurve3d* g, Outline& poly)
    {
        if (g->isKindOf(AcGe::kCompositeCrv3d))
        {
            AcGeVoidPointerArray list;
            static_cast<const AcGeCompositeCurve3d*>(g)->getCurveList(list);
            for (int i = 0; i < list.length(); ++i)
                AppendGeCurve(static_cast<const AcGeCurve3d*>(list[i]), poly);
            return;
        }

        AcGeInterval iv;
        g->getInterval(iv);
        if (!iv.isBounded()) return;
        double lo = iv.lowerBound(), hi = iv.upperBound();

        if (g->isKindOf(AcGe::kLineSeg3d))
        {
            Add(poly, g->evalPoint(lo));
            return;
        }

        int n = 64;
        if (g->isKindOf(AcGe::kCircArc3d))
        {
            auto* arc = static_cast<const AcGeCircArc3d*>(g);
            n = ArcSegments(arc->endAng() - arc->startAng());
        }
        for (int i = 0; i < n; ++i)
            Add(poly, g->evalPoint(lo + (hi - lo) * i / n));
    }

    bool FromPolyline(const AcDbPolyline* pl, Outline& poly)
    {
        unsigned int n = pl->numVerts();
        if (n < 2) return false;
        unsigned int segs = pl->isClosed() ? n : n - 1;
        for (unsigned int i = 0; i < segs; ++i)
        {
            double bulge = 0.0;
            pl->getBulgeAt(i, bulge);
            AcGePoint3d s;
            pl->getPointAt(i, s);
            if (std::fabs(bulge) < 1e-12)
            {
                Add(poly, s);
                continue;
            }
            AcGeCircArc3d arc;
            if (pl->getArcSegAt(i, arc) != Acad::eOk) { Add(poly, s); continue; }
            Add(poly, s);
            // Walk from the segment's start vertex to its end vertex,
            // whichever way the arc runs.
            AcGePoint3d e;
            pl->getPointAt((i + 1) % n, e);
            double ps = arc.paramOf(s), pe = arc.paramOf(e);
            int k = ArcSegments(pe - ps);
            for (int j = 1; j < k; ++j)
                Add(poly, arc.evalPoint(ps + (pe - ps) * j / k));
        }
        return true;
    }

    double SegDistance(const AcGePoint2d& p, const AcGePoint2d& a, const AcGePoint2d& b)
    {
        AcGeVector2d ab = b - a;
        double len2 = ab.lengthSqrd();
        double t = len2 > 0.0 ? (p - a).dotProduct(ab) / len2 : 0.0;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        return p.distanceTo(a + ab * t);
    }
}

bool GetBoundary(AcDbObjectId id, Outline& poly, CString& err)
{
    poly.clear();
    CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
    if (!ent) { err = _T("handle not found"); return false; }

    auto* curve = AcDbCurve::cast(ent.get());
    if (!curve) { err = _T("boundary must be a closed curve or an ACA space"); return false; }

    if (auto* pl = AcDbPolyline::cast(ent.get()))
    {
        AcGePoint3d s, e;
        bool closed = pl->isClosed()
            || (pl->getStartPoint(s) == Acad::eOk && pl->getEndPoint(e) == Acad::eOk && s.isEqualTo(e));
        if (!closed) { err = _T("boundary polyline is not closed"); return false; }
        FromPolyline(pl, poly);
    }
    else
    {
        if (!curve->isClosed()) { err = _T("boundary curve is not closed"); return false; }
        AcGeCurve3d* g = nullptr;
        if (curve->getAcGeCurve(g) == Acad::eOk && g)
        {
            AppendGeCurve(g, poly);
            delete g;
        }
        if (poly.size() < 3)   // no AcGe form: sample by parameter
        {
            poly.clear();
            double p0 = 0.0, p1 = 0.0;
            curve->getStartParam(p0);
            curve->getEndParam(p1);
            const int n = 256;
            for (int i = 0; i < n; ++i)
            {
                AcGePoint3d p;
                if (curve->getPointAtParam(p0 + (p1 - p0) * i / n, p) == Acad::eOk)
                    Add(poly, p);
            }
        }
    }

    if (poly.size() > 1 && poly.front().isEqualTo(poly.back())) poly.pop_back();
    if (poly.size() < 3) { err = _T("boundary has fewer than 3 points"); return false; }
    return true;
}

bool Contains(const Outline& poly, const AcGePoint2d& p, double tol, double* dist)
{
    bool inside = false;
    double best = -1.0;
    size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const AcGePoint2d& a = poly[i];
        const AcGePoint2d& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)
            && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
        double d = SegDistance(p, a, b);
        if (best < 0.0 || d < best) best = d;
    }
    if (dist) *dist = best < 0.0 ? 0.0 : best;
    return inside || (tol > 0.0 && best >= 0.0 && best <= tol);
}

std::vector<AcDbObjectId> FilterInside(AcDbObjectId boundary, const Outline& poly,
                                       const std::vector<AcDbObjectId>& candidates, double tol)
{
    std::vector<AcDbObjectId> out;
    for (AcDbObjectId id : candidates)
    {
        if (id == boundary) continue;
        AcGePoint3d c;
        bool ok = false;
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
            AcDbExtents ext;
            if (ent && ent->getGeomExtents(ext) == Acad::eOk)
            {
                c = ext.minPoint() + (ext.maxPoint() - ext.minPoint()) / 2.0;
                ok = true;
            }
        }
        if (ok && Contains(poly, AcGePoint2d(c.x, c.y), tol))
            out.push_back(id);
    }
    return out;
}
}
