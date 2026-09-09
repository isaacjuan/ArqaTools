#include "StdAfx.h"
#include "SvgExportTools.h"
#include "CommonTools.h"
#include "dbents.h"
#include "dbpl.h"
#include "dbsymtb.h"
#include "dbhatch.h"
#include "acestext.h"
#include <ShlObj.h>
#include <vector>
#include <algorithm>

namespace
{
    // -------------------------------------------------------------------------
    // Bounds — WCS bounding box of the exported selection. The SVG viewBox is
    // sized to this box, and every point is mapped through it with a Y-flip
    // (CAD is Y-up, SVG is Y-down). Because the flip is a pure reflection, any
    // angle taken directly from CAD geometry (arc sweep direction, text
    // rotation) must be negated when reused in SVG — see ToSvg()/EmitArc().
    // -------------------------------------------------------------------------
    struct Bounds
    {
        double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
        bool   valid = false;

        void Expand(const AcGePoint3d& p)
        {
            if (!valid) { minX = maxX = p.x; minY = maxY = p.y; valid = true; return; }
            minX = (std::min)(minX, p.x); maxX = (std::max)(maxX, p.x);
            minY = (std::min)(minY, p.y); maxY = (std::max)(maxY, p.y);
        }

        void Expand(const AcDbExtents& ext)
        {
            Expand(ext.minPoint());
            Expand(ext.maxPoint());
        }

        double Width()  const { return maxX - minX; }
        double Height() const { return maxY - minY; }
    };

    struct SvgPt { double x, y; };

    SvgPt ToSvg(const Bounds& b, const AcGePoint3d& p)
    {
        return { p.x - b.minX, b.Height() - (p.y - b.minY) };
    }

    CString FormatHex(int r, int g, int b)
    {
        CString s;
        s.Format(_T("#%02X%02X%02X"), r, g, b);
        return s;
    }

    // Resolves an entity's color to a concrete RGB, given an optional
    // already-resolved color to substitute for ByBlock (the color of the
    // block reference the entity is being expanded from). Falls back to
    // black when no fixed color can be determined either way.
    AcCmColor ResolveColor(AcDbEntity* pEnt, const AcCmColor* inheritedByBlock)
    {
        AcCmColor col = pEnt->color();
        if (col.isByBlock())
        {
            if (inheritedByBlock) col = *inheritedByBlock;
            else                  col.setRGB(0, 0, 0);
        }
        if (col.isByLayer())
        {
            CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(pEnt->layerId(), AcDb::kForRead);
            if (layer) col = layer->color();
        }
        if (col.isByLayer() || col.isByBlock())
            col.setRGB(0, 0, 0);
        return col;
    }

    CString ColorHex(AcDbEntity* pEnt, const AcCmColor* inheritedByBlock)
    {
        AcCmColor c = ResolveColor(pEnt, inheritedByBlock);
        return FormatHex(c.red(), c.green(), c.blue());
    }

    CString EscapeXml(const CString& in)
    {
        CString out;
        for (int i = 0; i < in.GetLength(); ++i)
        {
            TCHAR c = in[i];
            switch (c)
            {
                case _T('&'):  out += _T("&amp;");  break;
                case _T('<'):  out += _T("&lt;");   break;
                case _T('>'):  out += _T("&gt;");   break;
                case _T('"'):  out += _T("&quot;"); break;
                case _T('\''): out += _T("&apos;"); break;
                default:       out += c;
            }
        }
        return out;
    }

    // Best-effort strip of MText inline formatting codes (\C1;, \H2.5x;,
    // \f Arial|b0i0;, {...} grouping, \P paragraph breaks). Not a full MText
    // parser — good enough to get plain readable text for the common cases.
    CString StripMTextCodes(const CString& in)
    {
        CString out;
        int i = 0, n = in.GetLength();
        while (i < n)
        {
            TCHAR c = in[i];
            if (c == _T('\\') && i + 1 < n)
            {
                TCHAR next = in[i + 1];
                if (next == _T('P') || next == _T('p')) { out += _T(' '); i += 2; continue; }
                if (next == _T('\\') || next == _T('{') || next == _T('}'))
                { out += next; i += 2; continue; }
                if (_istalpha(next))
                {
                    int j = i + 2;
                    while (j < n && in[j] != _T(';') && in[j] != _T('\\') &&
                           in[j] != _T('{') && in[j] != _T('}'))
                        ++j;
                    if (j < n && in[j] == _T(';')) ++j;
                    i = j;
                    continue;
                }
            }
            if (c == _T('{') || c == _T('}')) { ++i; continue; }
            out += c;
            ++i;
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // Arc geometry — bulge -> (radius, includedAngle) per the standard
    // AutoCAD polyline-bulge definition: bulge = tan(includedAngle / 4),
    // chord = 2 * radius * sin(includedAngle / 2).
    // -------------------------------------------------------------------------
    void BulgeToArc(const AcGePoint2d& p0, const AcGePoint2d& p1, double bulge,
                     double& radius, double& includedAngle)
    {
        double d = p0.distanceTo(p1);
        includedAngle = 4.0 * atan(fabs(bulge));
        double s = sin(includedAngle / 2.0);
        radius = (fabs(s) > 1e-9) ? (d / (2.0 * s)) : 0.0;
    }

    // Appends an SVG elliptical-arc segment ("A rx,ry ...") to a path's "d"
    // attribute. AutoCAD arcs and positive-bulge polyline segments always
    // sweep CCW; the Y-flip in ToSvg() always reverses the visual sweep
    // sense, so a CCW CAD sweep maps to SVG sweep-flag 0, and a CW
    // (negative-bulge) sweep maps to sweep-flag 1.
    void AppendArcPath(CString& d, const Bounds& b, double radius, double includedAngle,
                        bool sweepIsCwInCad, const AcGePoint3d& endPt)
    {
        int largeArcFlag = (includedAngle > M_PI) ? 1 : 0;
        int sweepFlag    = sweepIsCwInCad ? 1 : 0;
        SvgPt e = ToSvg(b, endPt);
        CString seg;
        seg.Format(_T(" A%.4f,%.4f 0 %d,%d %.4f,%.4f"),
                   radius, radius, largeArcFlag, sweepFlag, e.x, e.y);
        d += seg;
    }

    // -------------------------------------------------------------------------
    // Per-entity-type emitters. Each takes an optional `inherited` color —
    // the already-resolved color of the block reference an entity is being
    // expanded from, used only when the entity's own color is ByBlock.
    // -------------------------------------------------------------------------
    void EmitLine(AcDbLine* p, const Bounds& b, double strokeWidth, CString& out,
                  const AcCmColor* inherited)
    {
        SvgPt a = ToSvg(b, p->startPoint());
        SvgPt c = ToSvg(b, p->endPoint());
        CString elem;
        elem.Format(_T("  <line x1=\"%.4f\" y1=\"%.4f\" x2=\"%.4f\" y2=\"%.4f\" ")
                    _T("stroke=\"%s\" stroke-width=\"%.4f\" />\n"),
                    a.x, a.y, c.x, c.y, (LPCTSTR)ColorHex(p, inherited), strokeWidth);
        out += elem;
    }

    void EmitCircle(AcDbCircle* p, const Bounds& b, double strokeWidth, CString& out,
                     const AcCmColor* inherited)
    {
        SvgPt c = ToSvg(b, p->center());
        CString elem;
        elem.Format(_T("  <circle cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\" ")
                    _T("stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    c.x, c.y, p->radius(), (LPCTSTR)ColorHex(p, inherited), strokeWidth);
        out += elem;
    }

    // AcDbPoint has no boundary geometry to trace, so it's rendered as a
    // small filled marker (radius = 2x stroke width) rather than an outline —
    // a plain approximation of the PDMODE glyph AutoCAD actually draws.
    void EmitPoint(AcDbPoint* p, const Bounds& b, double strokeWidth, CString& out,
                    const AcCmColor* inherited)
    {
        SvgPt c = ToSvg(b, p->position());
        CString elem;
        elem.Format(_T("  <circle cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\" fill=\"%s\" stroke=\"none\" />\n"),
                    c.x, c.y, strokeWidth * 2.0, (LPCTSTR)ColorHex(p, inherited));
        out += elem;
    }

    void EmitArc(AcDbArc* p, const Bounds& b, double strokeWidth, CString& out,
                 const AcCmColor* inherited)
    {
        AcGePoint3d center = p->center();
        double r = p->radius();
        double a0 = p->startAngle(), a1 = p->endAngle();
        double included = a1 - a0;
        while (included < 0.0)        included += 2.0 * M_PI;
        while (included > 2.0 * M_PI) included -= 2.0 * M_PI;

        AcGePoint3d startPt(center.x + r * cos(a0), center.y + r * sin(a0), center.z);
        AcGePoint3d endPt  (center.x + r * cos(a1), center.y + r * sin(a1), center.z);
        SvgPt s = ToSvg(b, startPt);

        CString d;
        d.Format(_T("M%.4f,%.4f"), s.x, s.y);
        AppendArcPath(d, b, r, included, /*sweepIsCwInCad=*/false, endPt);

        CString elem;
        elem.Format(_T("  <path d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited), strokeWidth);
        out += elem;
    }

    // Builds an SVG path "d" string from a vertex/bulge polygon (shared by
    // AcDbPolyline, AcDb2dPolyline, and AcDbHatch's polyline-type loops).
    // Ignores elevation/normal (assumes a WCS-aligned 2D drawing), same
    // simplification the rest of this exporter already makes.
    CString BuildPathFromVertices(const Bounds& b, const AcGePoint2dArray& pts,
                                   const AcGeDoubleArray& bulges, bool closed)
    {
        int n = pts.length();
        if (n < 2) return CString();

        SvgPt start = ToSvg(b, AcGePoint3d(pts[0].x, pts[0].y, 0.0));
        CString d;
        d.Format(_T("M%.4f,%.4f"), start.x, start.y);

        int segCount = closed ? n : n - 1;
        for (int i = 0; i < segCount; ++i)
        {
            int next = (i + 1) % n;
            double bulge = (i < bulges.length()) ? bulges[i] : 0.0;
            AcGePoint3d nextPt3(pts[next].x, pts[next].y, 0.0);

            if (fabs(bulge) < 1e-9)
            {
                SvgPt sp = ToSvg(b, nextPt3);
                CString seg;
                seg.Format(_T(" L%.4f,%.4f"), sp.x, sp.y);
                d += seg;
            }
            else
            {
                double radius, included;
                BulgeToArc(pts[i], pts[next], bulge, radius, included);
                AppendArcPath(d, b, radius, included, /*sweepIsCwInCad=*/bulge < 0.0, nextPt3);
            }
        }
        if (closed) d += _T(" Z");
        return d;
    }

    void EmitPolyline(AcDbPolyline* p, const Bounds& b, double strokeWidth, CString& out,
                       const AcCmColor* inherited)
    {
        unsigned int nVerts = p->numVerts();
        if (nVerts < 2) return;

        AcGePoint2dArray pts;
        AcGeDoubleArray bulges;
        for (unsigned int i = 0; i < nVerts; ++i)
        {
            AcGePoint2d pt; double bulge = 0.0;
            p->getPointAt(i, pt);
            p->getBulgeAt(i, bulge);
            pts.append(pt);
            bulges.append(bulge);
        }

        CString d = BuildPathFromVertices(b, pts, bulges, p->isClosed() != 0);
        if (d.IsEmpty()) return;

        CString elem;
        elem.Format(_T("  <path d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited), strokeWidth);
        out += elem;
    }

    // AcDb2dPolyline — the old "heavy" polyline, whose vertices are separate
    // database-resident AcDb2dVertex sub-entities rather than an inline
    // array. Only regular (line/arc) vertices are used; spline/curve-fit
    // helper vertices are skipped.
    void EmitHeavyPolyline(AcDb2dPolyline* p, const Bounds& b, double strokeWidth, CString& out,
                            const AcCmColor* inherited)
    {
        AcGePoint2dArray pts;
        AcGeDoubleArray bulges;

        CommonTools::AcDbIteratorGuard<AcDbObjectIterator> it(p->vertexIterator());
        if (it)
        {
            for (; !it->done(); it->step())
            {
                CommonTools::AcDbObjectGuard<AcDb2dVertex> pV(it->objectId(), AcDb::kForRead);
                if (!pV || pV->vertexType() != AcDb::k2dVertex) continue;
                AcGePoint3d pos = pV->position();
                pts.append(AcGePoint2d(pos.x, pos.y));
                bulges.append(pV->bulge());
            }
        }

        CString d = BuildPathFromVertices(b, pts, bulges, p->isClosed() != 0);
        if (d.IsEmpty()) return;

        CString elem;
        elem.Format(_T("  <path d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited), strokeWidth);
        out += elem;
    }

    // AcDbHatch — only polyline-type loops (straight/arc-bulge boundaries,
    // the common case for a wall's poché fill) are supported; loops made of
    // separate line/arc/spline edge curves are skipped. Multiple loops are
    // combined into one path with an even-odd fill rule so island/hole loops
    // (e.g. a wall opening) cut out correctly. Approximates the hatch as a
    // solid fill in the entity's own color — actual hatch patterns (cross-
    // hatching etc.) are not replicated.
    bool EmitHatch(AcDbHatch* p, const Bounds& b, CString& out, const AcCmColor* inherited)
    {
        CString allPaths;
        int numLoops = p->numLoops();
        for (int li = 0; li < numLoops; ++li)
        {
            if (!(p->loopTypeAt(li) & AcDbHatch::kPolyline)) continue;

            Adesk::Int32 loopType = 0;
            AcGePoint2dArray verts;
            AcGeDoubleArray bulges;
            if (p->getLoopAt(li, loopType, verts, bulges) != Acad::eOk) continue;

            CString d = BuildPathFromVertices(b, verts, bulges, /*closed=*/true);
            allPaths += d;
        }
        if (allPaths.IsEmpty()) return false;

        CString elem;
        elem.Format(_T("  <path d=\"%s\" fill=\"%s\" fill-rule=\"evenodd\" stroke=\"none\" />\n"),
                    (LPCTSTR)allPaths, (LPCTSTR)ColorHex(p, inherited));
        out += elem;
        return true;
    }

    // Shared by AcDbText and AcDbMText: places a <text> element at its CAD
    // insertion point/rotation. The rotation is negated for the same reason
    // arc sweeps are (see AppendArcPath) — the Y-flip reverses visual sense.
    void EmitTextElement(const AcGePoint3d& pos, double rotationRad, double heightPx,
                          const CString& content, AcDbEntity* pColorSrc,
                          const AcCmColor* inherited, const Bounds& b, CString& out)
    {
        SvgPt p = ToSvg(b, pos);
        double rotDeg = -(rotationRad * 180.0 / M_PI);
        CString elem;
        elem.Format(_T("  <text x=\"%.4f\" y=\"%.4f\" font-size=\"%.4f\" fill=\"%s\" ")
                    _T("transform=\"rotate(%.4f,%.4f,%.4f)\">%s</text>\n"),
                    p.x, p.y, heightPx, (LPCTSTR)ColorHex(pColorSrc, inherited),
                    rotDeg, p.x, p.y, (LPCTSTR)content);
        out += elem;
    }

    void EmitText(AcDbText* p, const Bounds& b, CString& out, const AcCmColor* inherited)
    {
        EmitTextElement(p->position(), p->rotation(), p->height(),
                        EscapeXml(p->textString()), p, inherited, b, out);
    }

    void EmitMText(AcDbMText* p, const Bounds& b, CString& out, const AcCmColor* inherited)
    {
        EmitTextElement(p->location(), p->rotation(), p->textHeight(),
                        EscapeXml(StripMTextCodes(p->contents())), p, inherited, b, out);
    }

    // -------------------------------------------------------------------------
    // Entity dispatch, shared by top-level selection entities and entities
    // expanded from a block definition or from explode(). Expansion (into a
    // nested block reference, or via explode()) recurses fully — real
    // drawings commonly nest several levels deep (a door block containing a
    // hardware sub-block, a dynamic block's anonymous block, etc.) — but
    // `depth` is capped well below any plausible legitimate nesting as a
    // guard against a cyclic block definition or pathological data.
    // -------------------------------------------------------------------------
    constexpr int kMaxExpansionDepth = 32;

    void EmitBlockRef(AcDbBlockReference* pRef, const Bounds& b, double strokeWidth,
                       CString& out, int depth, int& exported, int& skipped);

    // Fallback for any entity type not natively recognized above — custom /
    // ObjectDBX classes such as AutoCAD Architecture's AEC_WALL, AEC_DOOR,
    // AEC_WINDOW (or a proxy standing in for an unavailable object enabler).
    // explode() is the ObjectARX-standard way to get an object's "as
    // displayed" geometry without needing its proprietary API — the same
    // mechanism the EXPLODE command uses.
    void EmitExploded(AcDbEntity* pEnt, const Bounds& b, double strokeWidth, CString& out,
                       const AcCmColor* inherited, int depth, int& exported, int& skipped);

    CString ClassName(AcDbEntity* pEnt)
    {
        AcRxClass* pClass = pEnt->isA();
        return pClass ? CString(pClass->name()) : CString(_T("<unknown>"));
    }

    void EmitEntity(AcDbEntity* pEnt, const Bounds& b, double strokeWidth, CString& out,
                     const AcCmColor* inherited, int depth,
                     int& exported, int& skipped)
    {
        bool allowExpansion = depth < kMaxExpansionDepth;

        if      (pEnt->isKindOf(AcDbLine::desc()))
        { EmitLine(static_cast<AcDbLine*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbCircle::desc()))
        { EmitCircle(static_cast<AcDbCircle*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbArc::desc()))
        { EmitArc(static_cast<AcDbArc*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbPolyline::desc()))
        { EmitPolyline(static_cast<AcDbPolyline*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDb2dPolyline::desc()))
        { EmitHeavyPolyline(static_cast<AcDb2dPolyline*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbHatch::desc()))
        {
            if (EmitHatch(static_cast<AcDbHatch*>(pEnt), b, out, inherited)) ++exported;
            else { acutPrintf(_T("  [skip] AcDbHatch: no polyline-type loop found\n")); ++skipped; }
        }
        else if (pEnt->isKindOf(AcDbText::desc()))
        { EmitText(static_cast<AcDbText*>(pEnt), b, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbMText::desc()))
        { EmitMText(static_cast<AcDbMText*>(pEnt), b, out, inherited); ++exported; }
        else if (pEnt->isKindOf(AcDbPoint::desc()))
        { EmitPoint(static_cast<AcDbPoint*>(pEnt), b, strokeWidth, out, inherited); ++exported; }
        else if (allowExpansion && pEnt->isKindOf(AcDbBlockReference::desc()))
        { EmitBlockRef(static_cast<AcDbBlockReference*>(pEnt), b, strokeWidth, out, depth, exported, skipped); }
        else if (allowExpansion)
        { EmitExploded(pEnt, b, strokeWidth, out, inherited, depth, exported, skipped); }
        else
        {
            acutPrintf(_T("  [skip] %s (depth limit or unsupported type)\n"), (LPCTSTR)ClassName(pEnt));
            ++skipped;
        }
    }

    // Expands a block reference's definition (Line/Circle/Arc/Polyline/Text/
    // MText/Hatch/2dPolyline directly; a nested block reference or unknown
    // custom object recurses through EmitBlockRef/EmitExploded again, up to
    // kMaxExpansionDepth), transforming each entity by the reference's
    // block-to-WCS matrix before emitting it. ByBlock colors resolve to this
    // reference's own resolved color.
    void EmitBlockRef(AcDbBlockReference* pRef, const Bounds& b, double strokeWidth,
                       CString& out, int depth, int& exported, int& skipped)
    {
        CommonTools::AcDbObjectGuard<AcDbBlockTableRecord> def(pRef->blockTableRecord(), AcDb::kForRead);
        if (!def) { ++skipped; return; }

        AcDbBlockTableRecordIterator* pIter = nullptr;
        if (def->newIterator(pIter) != Acad::eOk) { ++skipped; return; }
        CommonTools::AcDbIteratorGuard<AcDbBlockTableRecordIterator> iterGuard(pIter);

        AcCmColor refColor = ResolveColor(pRef, nullptr);
        AcGeMatrix3d xform = pRef->blockTransform();

        for (; !iterGuard->done(); iterGuard->step())
        {
            AcDbEntity* pSub = nullptr;
            if (iterGuard->getEntity(pSub, AcDb::kForRead) != Acad::eOk) continue;

            AcDbEntity* pClone = static_cast<AcDbEntity*>(pSub->clone());
            pSub->close();
            if (!pClone) { ++skipped; continue; }

            pClone->transformBy(xform);
            EmitEntity(pClone, b, strokeWidth, out, &refColor, depth + 1, exported, skipped);
            delete pClone;
        }
    }

    // Explodes an unrecognized entity into its displayed representation and
    // emits each resulting piece. Exploded pieces commonly carry a ByBlock
    // color inherited from the original object, so the original's own
    // resolved color (given whatever inheritance already applied to it) is
    // threaded through as the pieces' inherited color.
    void EmitExploded(AcDbEntity* pEnt, const Bounds& b, double strokeWidth, CString& out,
                       const AcCmColor* inherited, int depth, int& exported, int& skipped)
    {
        AcDbVoidPtrArray pieces;
        Acad::ErrorStatus es = pEnt->explode(pieces);
        if (es != Acad::eOk || pieces.length() == 0)
        {
            acutPrintf(_T("  [skip] %s: explode() -> %s, %d piece(s)\n"),
                       (LPCTSTR)ClassName(pEnt), acadErrorStatusText(es), pieces.length());
            ++skipped;
            return;
        }

        AcCmColor selfColor = ResolveColor(pEnt, inherited);
        for (int i = 0; i < pieces.length(); ++i)
        {
            AcDbEntity* pPiece = static_cast<AcDbEntity*>(pieces[i]);
            if (!pPiece) continue;
            EmitEntity(pPiece, b, strokeWidth, out, &selfColor, depth + 1, exported, skipped);
            delete pPiece;
        }
    }
}

void SvgExportTools::svgExportCommand()
{
    acutPrintf(_T("\n=== SVG EXPORT ===\n"));

    CommonTools::SelectionSetGuard ssGuard;
    if (!ssGuard.Get())
    { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

    Adesk::Int32 length = 0;
    acedSSLength(ssGuard.ss, &length);
    if (length == 0)
    { acutPrintf(CommonTools::MSG_NO_SELECTION); return; }

    std::vector<AcDbObjectId> ids;
    ids.reserve(length);
    CommonTools::ForEachSsEntity(ssGuard.ss, length, [&](AcDbObjectId id) { ids.push_back(id); });

    // Pass 1: bounding box, to size the viewBox and set up the Y-flip.
    // getGeomExtents on a block reference already accounts for its transform,
    // so nested block content is included without any special-casing here.
    Bounds bounds;
    for (auto id : ids)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForRead);
        if (!ent) continue;
        AcDbExtents ext;
        if (ent->getGeomExtents(ext) == Acad::eOk)
            bounds.Expand(ext);
    }
    if (!bounds.valid)
    { acutPrintf(_T("\nCould not determine extents of the selection.\n")); return; }

    // Guard against a degenerate viewBox for a single-point selection.
    double width       = (std::max)(bounds.Width(),  1.0);
    double height       = (std::max)(bounds.Height(), 1.0);
    double strokeWidth = (std::max)(width, height) * 0.002;

    // Pass 2: emit supported entities.
    CString body;
    int exported = 0, skipped = 0;
    for (auto id : ids)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForRead);
        if (!ent) { ++skipped; continue; }

        EmitEntity(ent.get(), bounds, strokeWidth, body, /*inherited=*/nullptr,
                   /*depth=*/0, exported, skipped);
    }

    if (exported == 0)
    {
        acutPrintf(_T("\nNo exportable geometry in the selection — nothing recognized ")
                   _T("directly, and explode() produced nothing for the rest.\n"));
        return;
    }

    CString svg;
    svg.Format(_T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
               _T("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.4f %.4f\" ")
               _T("width=\"%.4f\" height=\"%.4f\">\n"),
               width, height, width, height);
    svg += body;
    svg += _T("</svg>\n");

    TCHAR docPath[MAX_PATH];
    SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
    CString filePath;
    filePath.Format(_T("%s\\ArqaTools_Export.svg"), docPath);

    FILE* fp = nullptr;
    errno_t err = _tfopen_s(&fp, filePath, _T("w, ccs=UTF-8"));
    if (err != 0 || !fp)
    { acutPrintf(_T("\nError: could not write to %s\n"), (LPCTSTR)filePath); return; }
    fwprintf(fp, _T("%s"), (LPCTSTR)svg);
    fclose(fp);

    acutPrintf(_T("\nExported %d entities (%d skipped) to:\n%s\n"),
               exported, skipped, (LPCTSTR)filePath);
}
