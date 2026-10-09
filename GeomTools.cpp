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

namespace
{
    AcGePoint2d ClosestOnSeg(const AcGePoint2d& p, const AcGePoint2d& a, const AcGePoint2d& b)
    {
        AcGeVector2d ab = b - a;
        double len2 = ab.lengthSqrd();
        double t = len2 > 0.0 ? (p - a).dotProduct(ab) / len2 : 0.0;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        return a + ab * t;
    }

    double Cross(const AcGePoint2d& o, const AcGePoint2d& a, const AcGePoint2d& b)
    {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    // Segment-segment distance with the closest points (0 when they cross).
    double SegSeg(const AcGePoint2d& a1, const AcGePoint2d& a2,
                  const AcGePoint2d& b1, const AcGePoint2d& b2,
                  AcGePoint2d& pa, AcGePoint2d& pb)
    {
        double d1 = Cross(a1, a2, b1), d2 = Cross(a1, a2, b2);
        double d3 = Cross(b1, b2, a1), d4 = Cross(b1, b2, a2);
        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        {
            double t = d3 / (d3 - d4);
            pa = pb = a1 + (a2 - a1) * t;
            return 0.0;
        }
        double best = -1.0;
        auto tryPair = [&](const AcGePoint2d& p, const AcGePoint2d& q) {
            double d = p.distanceTo(q);
            if (best < 0.0 || d < best) { best = d; pa = p; pb = q; }
        };
        tryPair(a1, ClosestOnSeg(a1, b1, b2));
        tryPair(a2, ClosestOnSeg(a2, b1, b2));
        tryPair(ClosestOnSeg(b1, a1, a2), b1);
        tryPair(ClosestOnSeg(b2, a1, a2), b2);
        return best;
    }

    bool IsAecNonSpace(const AcRxClass* c)
    {
        return c && c->name() && _tcsncmp(c->name(), _T("Aec"), 3) == 0
            && _tcscmp(c->name(), _T("AecDbSpace")) != 0;
    }
}

bool GetOutline(AcDbObjectId id, Outline& pts, bool& closed, CString& err)
{
    pts.clear();
    closed = false;
    CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
    if (!ent) { err = _T("handle not found"); return false; }

    auto* curve = AcDbCurve::cast(ent.get());
    if (curve && !IsAecNonSpace(id.objectClass()))
    {
        if (auto* pl = AcDbPolyline::cast(ent.get()))
        {
            FromPolyline(pl, pts);
            closed = pl->isClosed();
            if (!closed)
            {
                AcGePoint3d e;
                if (pl->getEndPoint(e) == Acad::eOk) Add(pts, e);
            }
        }
        else
        {
            closed = curve->isClosed();
            AcGeCurve3d* g = nullptr;
            if (curve->getAcGeCurve(g) == Acad::eOk && g)
            {
                AppendGeCurve(g, pts);
                delete g;
            }
            if (pts.size() < 2)
            {
                pts.clear();
                double p0 = 0.0, p1 = 0.0;
                curve->getStartParam(p0);
                curve->getEndParam(p1);
                const int n = 256;
                for (int i = 0; i < n; ++i)
                {
                    AcGePoint3d p;
                    if (curve->getPointAtParam(p0 + (p1 - p0) * i / n, p) == Acad::eOk) Add(pts, p);
                }
            }
            if (!closed)
            {
                AcGePoint3d e;
                if (curve->getEndPoint(e) == Acad::eOk) Add(pts, e);
            }
        }
        if (closed && pts.size() > 1 && pts.front().isEqualTo(pts.back())) pts.pop_back();
        if (pts.size() >= 2) return true;
        pts.clear();   // degenerate curve: fall back to the extents
    }

    AcDbExtents ext;
    if (ent->getGeomExtents(ext) != Acad::eOk) { err = _T("object has no extents"); return false; }
    AcGePoint3d mn = ext.minPoint(), mx = ext.maxPoint();
    pts = { AcGePoint2d(mn.x, mn.y), AcGePoint2d(mx.x, mn.y), AcGePoint2d(mx.x, mx.y), AcGePoint2d(mn.x, mx.y) };
    closed = true;
    return true;
}

double Distance(const Outline& a, bool aClosed, const Outline& b, bool bClosed,
                AcGePoint2d* pa, AcGePoint2d* pb)
{
    AcGePoint2d qa, qb;
    // One inside the other (closed outlines): they overlap.
    if (bClosed && b.size() >= 3 && !a.empty() && Contains(b, a.front(), 0.0))
    {
        if (pa) *pa = a.front();
        if (pb) *pb = a.front();
        return 0.0;
    }
    if (aClosed && a.size() >= 3 && !b.empty() && Contains(a, b.front(), 0.0))
    {
        if (pa) *pa = b.front();
        if (pb) *pb = b.front();
        return 0.0;
    }

    size_t na = a.size(), nb = b.size();
    size_t sa = aClosed ? na : (na > 0 ? na - 1 : 0);
    size_t sb = bClosed ? nb : (nb > 0 ? nb - 1 : 0);
    double best = -1.0;
    if (sa == 0 || sb == 0)   // a single point on either side
    {
        for (size_t i = 0; i < na; ++i)
            for (size_t j = 0; j < nb; ++j)
            {
                double d = a[i].distanceTo(b[j]);
                if (best < 0.0 || d < best) { best = d; qa = a[i]; qb = b[j]; }
            }
    }
    for (size_t i = 0; i < sa; ++i)
        for (size_t j = 0; j < sb; ++j)
        {
            AcGePoint2d p, q;
            double d = SegSeg(a[i], a[(i + 1) % na], b[j], b[(j + 1) % nb], p, q);
            if (best < 0.0 || d < best) { best = d; qa = p; qb = q; }
        }
    if (best < 0.0) best = 0.0;
    if (pa) *pa = qa;
    if (pb) *pb = qb;
    return best;
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
