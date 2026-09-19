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
#include <map>
#include <algorithm>

namespace
{
    // -------------------------------------------------------------------------
    // Bounds — WCS bounding box of the exported selection. The SVG viewBox is
    // sized to this box, and every point is mapped through it with a Y-flip
    // (CAD is Y-up, SVG is Y-down). Because the flip is a pure reflection, any
    // angle taken directly from CAD geometry (arc sweep direction, text
    // rotation) must be negated when reused in SVG — see ToSvg()/EmitArc().
    //
    // A null Bounds* means "block-definition-local mode": geometry is being
    // written once into a shared <g> (see EmitTopLevelBlockRef) in the
    // block's own local space, Y-flipped but NOT translated by the overall
    // selection bounds — placement is done entirely by the per-instance
    // <use transform="matrix(...)">. The same null check doubles as the
    // signal for ColorHex() to defer a ByBlock color to CSS `currentColor`
    // instead of baking in a fixed hex, since only content going into a
    // shared definition needs that deferral.
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

    SvgPt ToSvg(const Bounds* b, const AcGePoint3d& p)
    {
        if (!b) return { p.x, -p.y };
        return { p.x - b->minX, b->Height() - (p.y - b->minY) };
    }

    // -------------------------------------------------------------------------
    // Style inheritance — the already-resolved color and lineweight of the
    // block reference (or exploded object) an entity is being expanded from,
    // used only when the entity's own color/lineweight is ByBlock.
    // -------------------------------------------------------------------------
    struct InheritedStyle
    {
        AcCmColor        color;
        AcDb::LineWeight lineWeight;
    };

    CString FormatHex(int r, int g, int b)
    {
        CString s;
        s.Format(_T("#%02X%02X%02X"), r, g, b);
        return s;
    }

    // Resolves an entity's color to a concrete RGB, given an optional
    // already-resolved inherited style to substitute for ByBlock. Falls back
    // to black when no fixed color can be determined either way.
    AcCmColor ResolveColor(AcDbEntity* pEnt, const InheritedStyle* inherited)
    {
        AcCmColor col = pEnt->color();
        if (col.isByBlock())
        {
            if (inherited) col = inherited->color;
            else           col.setRGB(0, 0, 0);
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

    // `b == nullptr` means this entity is being written into a shared block
    // <g> definition — a ByBlock color there defers to CSS `currentColor`
    // (set per-instance via the <use>'s own `color` attribute) instead of
    // being baked in, since the same definition is reused by every instance,
    // each of which may want a different resolved ByBlock color.
    CString ColorHex(AcDbEntity* pEnt, const InheritedStyle* inherited, const Bounds* b)
    {
        if (!b && pEnt->color().isByBlock())
            return _T("currentColor");
        AcCmColor c = ResolveColor(pEnt, inherited);
        return FormatHex(c.red(), c.green(), c.blue());
    }

    // Resolves an entity's lineweight to a concrete enum value (ByLayer via
    // its layer, ByBlock via the inherited style, any other unresolvable
    // sentinel — e.g. ByLwDefault — falls back to a plain 0.25mm default).
    AcDb::LineWeight ResolveLineWeightEnum(AcDbEntity* pEnt, const InheritedStyle* inherited)
    {
        AcDb::LineWeight lw = pEnt->lineWeight();
        if (lw == AcDb::kLnWtByBlock)
            lw = inherited ? inherited->lineWeight : AcDb::kLnWt025;
        if (lw == AcDb::kLnWtByLayer)
        {
            CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(pEnt->layerId(), AcDb::kForRead);
            lw = layer ? layer->lineWeight() : AcDb::kLnWt025;
        }
        if (lw < 0) lw = AcDb::kLnWt025;
        return lw;
    }

    // AutoCAD lineweight values are hundredths of a millimeter regardless of
    // drawing units (LineWeight enum values ARE that number, e.g. kLnWt025 ==
    // 25 == 0.25mm) — unitsPerMm (from the database's INSUNITS) converts that
    // into the drawing units the exported coordinates are already in. A zero
    // (or otherwise non-positive) weight is floored to a thin-but-visible
    // hairline rather than an invisible zero-width stroke.
    double LineWeightToUnits(AcDb::LineWeight lw, double unitsPerMm)
    {
        double mm = (double)lw / 100.0;
        if (mm <= 0.0) mm = 0.05;
        return mm * unitsPerMm;
    }

    double ResolveStrokeWidth(AcDbEntity* pEnt, double unitsPerMm, const InheritedStyle* inherited)
    {
        return LineWeightToUnits(ResolveLineWeightEnum(pEnt, inherited), unitsPerMm);
    }

    // Drawing-units-per-millimeter for the database's INSUNITS setting.
    // Covers the units an architectural/engineering drawing is realistically
    // set to; anything else (including kUnitsUndefined) is treated as
    // millimeters, a reasonable default for an unset/exotic unit.
    double UnitsPerMm(AcDb::UnitsValue u)
    {
        switch (u)
        {
            case AcDb::kUnitsMillimeters:  return 1.0;
            case AcDb::kUnitsCentimeters:  return 0.1;
            case AcDb::kUnitsDecimeters:   return 0.01;
            case AcDb::kUnitsMeters:       return 0.001;
            case AcDb::kUnitsDekameters:   return 0.0001;
            case AcDb::kUnitsHectometers:  return 0.00001;
            case AcDb::kUnitsKilometers:   return 0.000001;
            case AcDb::kUnitsMicrons:      return 1000.0;
            case AcDb::kUnitsMils:         return 1.0 / 0.0254;
            case AcDb::kUnitsMicroinches:  return 1.0 / 0.0000254;
            case AcDb::kUnitsInches:
            case AcDb::kUnitsUSSurveyInch: return 1.0 / 25.4;
            case AcDb::kUnitsFeet:
            case AcDb::kUnitsUSSurveyFeet: return 1.0 / 304.8;
            case AcDb::kUnitsYards:
            case AcDb::kUnitsUSSurveyYard: return 1.0 / 914.4;
            case AcDb::kUnitsMiles:
            case AcDb::kUnitsUSSurveyMile: return 1.0 / 1609344.0;
            default:                       return 1.0;
        }
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
    // Element identity — every emitted element gets an `id` derived from its
    // source AutoCAD entity's handle (the same persistent identifier the LIST
    // command shows), prefixed "e" since a handle can start with a digit.
    // Entities read directly from the database (top-level selections, a block
    // definition's own sub-entities) have a real, stable handle. Content that
    // doesn't — a clone made to apply a nested block's transform, or a piece
    // produced by explode() — has no handle of its own, so the id is instead
    // derived from the real database entity it came from (captured as
    // `sourceId` before cloning/exploding), with a "_N" suffix from
    // EmitExploded when one entity yields multiple pieces.
    // -------------------------------------------------------------------------
    CString HandleId(AcDbEntity* pEnt)
    {
        AcDbHandle h;
        pEnt->getAcDbHandle(h);
        TCHAR buf[AcDbHandle::kStrSiz];
        h.getIntoAsciiBuffer(buf);
        CString s;
        s.Format(_T("e%s"), buf);
        return s;
    }

    // -------------------------------------------------------------------------
    // Layer grouping — every emitted element is bucketed by its own entity's
    // layer (never inherited/overridden by a containing block reference:
    // unlike color/lineweight, an entity's layer is always its own fixed
    // property in AutoCAD) into a LayerMap, later wrapped one <g> per layer
    // by WrapLayers(). This applies uniformly to top-level content and to
    // content inside a shared block <g> definition, so a block's internal
    // layer structure (if it uses more than one layer) is preserved too.
    // -------------------------------------------------------------------------
    using LayerMap = std::map<CString, CString>;

    CString LayerNameOf(AcDbEntity* pEnt)
    {
        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(pEnt->layerId(), AcDb::kForRead);
        return layer ? CString(layer->name()) : CString(_T("0"));
    }

    void AppendToLayer(LayerMap& layers, AcDbEntity* pEnt, const CString& elem)
    {
        layers[LayerNameOf(pEnt)] += elem;
    }

    // Sanitizes a layer name into a valid XML id/class-name fragment
    // (letters/digits/_/-/. only, not starting with a digit) — layer names
    // can contain spaces, '|' (xref-bound layers), '$', etc. The original
    // name is preserved verbatim in the group's data-layer-name attribute.
    CString SanitizeXmlId(const CString& name)
    {
        CString s;
        for (int i = 0; i < name.GetLength(); ++i)
        {
            TCHAR c = name[i];
            bool ok = (c >= _T('A') && c <= _T('Z')) || (c >= _T('a') && c <= _T('z')) ||
                      (c >= _T('0') && c <= _T('9')) || c == _T('_') || c == _T('-') || c == _T('.');
            s += ok ? c : _T('_');
        }
        if (s.IsEmpty() || (s[0] >= _T('0') && s[0] <= _T('9')))
            s = _T("L_") + s;
        return s;
    }

    // A layer group is written with `class`, not `id`: the same layer name
    // can legitimately recur in more than one independently-built LayerMap
    // (top-level content, and each block definition's own content each call
    // WrapLayers() separately) — reusing an `id` across those would violate
    // the SVG spec's document-wide id-uniqueness requirement (as happened
    // before this fix), whereas a class has no such constraint. It also
    // means a single selector (e.g. ".layer-muebles") finds every group for
    // that layer everywhere, including inside every block — arguably more
    // useful for layer-visibility toggling than a single unique element.
    CString WrapLayers(const LayerMap& layers)
    {
        CString result;
        for (const auto& kv : layers)
        {
            CString g;
            g.Format(_T("  <g class=\"layer layer-%s\" data-layer-name=\"%s\">\n%s  </g>\n"),
                     (LPCTSTR)SanitizeXmlId(kv.first), (LPCTSTR)EscapeXml(kv.first), (LPCTSTR)kv.second);
            result += g;
        }
        return result;
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
    void AppendArcPath(CString& d, const Bounds* b, double radius, double includedAngle,
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
    // Per-entity-type emitters. Each takes `unitsPerMm` (to resolve its own
    // actual AutoCAD lineweight into a stroke-width in drawing units), an
    // optional `inherited` style — the already-resolved color/lineweight of
    // the block reference an entity is being expanded from, used only when
    // the entity's own color/lineweight is ByBlock and `b` is non-null (see
    // ColorHex) — a LayerMap to append into (see AppendToLayer), and
    // `sourceId` (see HandleId) for the element's own `id`. `b` null means
    // "block-definition-local mode" (see Bounds comment above).
    // -------------------------------------------------------------------------
    void EmitLine(AcDbLine* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                  const InheritedStyle* inherited, const CString& sourceId)
    {
        SvgPt a = ToSvg(b, p->startPoint());
        SvgPt c = ToSvg(b, p->endPoint());
        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <line id=\"%s\" x1=\"%.4f\" y1=\"%.4f\" x2=\"%.4f\" y2=\"%.4f\" ")
                    _T("stroke=\"%s\" stroke-width=\"%.4f\" />\n"),
                    (LPCTSTR)sourceId, a.x, a.y, c.x, c.y, (LPCTSTR)ColorHex(p, inherited, b), sw);
        AppendToLayer(out, p, elem);
    }

    void EmitCircle(AcDbCircle* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                     const InheritedStyle* inherited, const CString& sourceId)
    {
        SvgPt c = ToSvg(b, p->center());
        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <circle id=\"%s\" cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\" ")
                    _T("stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)sourceId, c.x, c.y, p->radius(), (LPCTSTR)ColorHex(p, inherited, b), sw);
        AppendToLayer(out, p, elem);
    }

    // AcDbPoint has no boundary geometry to trace, so it's rendered as a
    // small filled marker (radius = 2x its own resolved stroke width) rather
    // than an outline — a plain approximation of the PDMODE glyph AutoCAD
    // actually draws.
    void EmitPoint(AcDbPoint* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                    const InheritedStyle* inherited, const CString& sourceId)
    {
        SvgPt c = ToSvg(b, p->position());
        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <circle id=\"%s\" cx=\"%.4f\" cy=\"%.4f\" r=\"%.4f\" fill=\"%s\" stroke=\"none\" />\n"),
                    (LPCTSTR)sourceId, c.x, c.y, sw * 2.0, (LPCTSTR)ColorHex(p, inherited, b));
        AppendToLayer(out, p, elem);
    }

    void EmitArc(AcDbArc* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                 const InheritedStyle* inherited, const CString& sourceId)
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

        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <path id=\"%s\" d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)sourceId, (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited, b), sw);
        AppendToLayer(out, p, elem);
    }

    // Builds an SVG path "d" string from a vertex/bulge polygon (shared by
    // AcDbPolyline, AcDb2dPolyline, and AcDbHatch's polyline-type loops).
    // Ignores elevation/normal (assumes a WCS-aligned 2D drawing), same
    // simplification the rest of this exporter already makes.
    CString BuildPathFromVertices(const Bounds* b, const AcGePoint2dArray& pts,
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

    void EmitPolyline(AcDbPolyline* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                       const InheritedStyle* inherited, const CString& sourceId)
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

        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <path id=\"%s\" d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)sourceId, (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited, b), sw);
        AppendToLayer(out, p, elem);
    }

    // AcDb2dPolyline — the old "heavy" polyline, whose vertices are separate
    // database-resident AcDb2dVertex sub-entities rather than an inline
    // array. Only regular (line/arc) vertices are used; spline/curve-fit
    // helper vertices are skipped.
    void EmitHeavyPolyline(AcDb2dPolyline* p, const Bounds* b, double unitsPerMm, LayerMap& out,
                            const InheritedStyle* inherited, const CString& sourceId)
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

        double sw = ResolveStrokeWidth(p, unitsPerMm, inherited);
        CString elem;
        elem.Format(_T("  <path id=\"%s\" d=\"%s\" stroke=\"%s\" stroke-width=\"%.4f\" fill=\"none\" />\n"),
                    (LPCTSTR)sourceId, (LPCTSTR)d, (LPCTSTR)ColorHex(p, inherited, b), sw);
        AppendToLayer(out, p, elem);
    }

    // AcDbHatch — only polyline-type loops (straight/arc-bulge boundaries,
    // the common case for a wall's poché fill) are supported; loops made of
    // separate line/arc/spline edge curves are skipped. Multiple loops are
    // combined into one path with an even-odd fill rule so island/hole loops
    // (e.g. a wall opening) cut out correctly. Approximates the hatch as a
    // solid fill in the entity's own color — actual hatch patterns (cross-
    // hatching etc.) are not replicated.
    bool EmitHatch(AcDbHatch* p, const Bounds* b, LayerMap& out, const InheritedStyle* inherited,
                   const CString& sourceId)
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
        elem.Format(_T("  <path id=\"%s\" d=\"%s\" fill=\"%s\" fill-rule=\"evenodd\" stroke=\"none\" />\n"),
                    (LPCTSTR)sourceId, (LPCTSTR)allPaths, (LPCTSTR)ColorHex(p, inherited, b));
        AppendToLayer(out, p, elem);
        return true;
    }

    // Shared by AcDbText and AcDbMText: places a <text> element at its CAD
    // insertion point/rotation. The rotation is negated for the same reason
    // arc sweeps are (see AppendArcPath) — the Y-flip reverses visual sense.
    void EmitTextElement(const AcGePoint3d& pos, double rotationRad, double heightPx,
                          const CString& content, AcDbEntity* pColorSrc,
                          const InheritedStyle* inherited, const Bounds* b, LayerMap& out,
                          const CString& sourceId)
    {
        SvgPt p = ToSvg(b, pos);
        double rotDeg = -(rotationRad * 180.0 / M_PI);
        CString elem;
        elem.Format(_T("  <text id=\"%s\" x=\"%.4f\" y=\"%.4f\" font-size=\"%.4f\" fill=\"%s\" ")
                    _T("transform=\"rotate(%.4f,%.4f,%.4f)\">%s</text>\n"),
                    (LPCTSTR)sourceId, p.x, p.y, heightPx, (LPCTSTR)ColorHex(pColorSrc, inherited, b),
                    rotDeg, p.x, p.y, (LPCTSTR)content);
        AppendToLayer(out, pColorSrc, elem);
    }

    void EmitText(AcDbText* p, const Bounds* b, LayerMap& out, const InheritedStyle* inherited,
                  const CString& sourceId)
    {
        EmitTextElement(p->position(), p->rotation(), p->height(),
                        EscapeXml(p->textString()), p, inherited, b, out, sourceId);
    }

    void EmitMText(AcDbMText* p, const Bounds* b, LayerMap& out, const InheritedStyle* inherited,
                   const CString& sourceId)
    {
        EmitTextElement(p->location(), p->rotation(), p->textHeight(),
                        EscapeXml(StripMTextCodes(p->contents())), p, inherited, b, out, sourceId);
    }

    // -------------------------------------------------------------------------
    // Entity dispatch, shared by top-level selection entities and entities
    // expanded from a block definition or from explode(). Expansion (into a
    // nested block reference, or via explode()) recurses fully — real
    // drawings commonly nest several levels deep (a door block containing a
    // hardware sub-block, a dynamic block's anonymous block, etc.) — but
    // `depth` is capped well below any plausible legitimate nesting as a
    // guard against a cyclic block definition or pathological data.
    //
    // A *directly selected* block reference does not come through here at
    // all — see EmitTopLevelBlockRef, which builds a shared <g> definition
    // once per unique block and a <use> per instance. This dispatch's own
    // AcDbBlockReference branch only fires for a block-within-a-block found
    // one or more levels down (while building such a definition, or from an
    // explode() result), where it's simplest to flatten the nested content
    // (composing transforms) directly into the enclosing <g> rather than
    // creating further nested <symbol>/<use> pairs.
    // -------------------------------------------------------------------------
    constexpr int kMaxExpansionDepth = 32;

    void EmitBlockRef(AcDbBlockReference* pRef, const Bounds* b, double unitsPerMm,
                       LayerMap& out, const CString& sourceId, int depth, int& exported, int& skipped);

    // Fallback for any entity type not natively recognized above — custom /
    // ObjectDBX classes such as AutoCAD Architecture's AEC_WALL, AEC_DOOR,
    // AEC_WINDOW (or a proxy standing in for an unavailable object enabler).
    // explode() is the ObjectARX-standard way to get an object's "as
    // displayed" geometry without needing its proprietary API — the same
    // mechanism the EXPLODE command uses. `sourceId` is the exploded entity's
    // own id, since the pieces themselves have no real handle of their own.
    void EmitExploded(AcDbEntity* pEnt, const Bounds* b, double unitsPerMm, LayerMap& out,
                       const InheritedStyle* inherited, const CString& sourceId,
                       int depth, int& exported, int& skipped);

    CString ClassName(AcDbEntity* pEnt)
    {
        AcRxClass* pClass = pEnt->isA();
        return pClass ? CString(pClass->name()) : CString(_T("<unknown>"));
    }

    void EmitEntity(AcDbEntity* pEnt, const Bounds* b, double unitsPerMm, LayerMap& out,
                     const InheritedStyle* inherited, const CString& sourceId, int depth,
                     int& exported, int& skipped)
    {
        bool allowExpansion = depth < kMaxExpansionDepth;

        if      (pEnt->isKindOf(AcDbLine::desc()))
        { EmitLine(static_cast<AcDbLine*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbCircle::desc()))
        { EmitCircle(static_cast<AcDbCircle*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbArc::desc()))
        { EmitArc(static_cast<AcDbArc*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbPolyline::desc()))
        { EmitPolyline(static_cast<AcDbPolyline*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDb2dPolyline::desc()))
        { EmitHeavyPolyline(static_cast<AcDb2dPolyline*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbHatch::desc()))
        {
            if (EmitHatch(static_cast<AcDbHatch*>(pEnt), b, out, inherited, sourceId)) ++exported;
            else { acutPrintf(_T("  [skip] AcDbHatch: no polyline-type loop found\n")); ++skipped; }
        }
        else if (pEnt->isKindOf(AcDbText::desc()))
        { EmitText(static_cast<AcDbText*>(pEnt), b, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbMText::desc()))
        { EmitMText(static_cast<AcDbMText*>(pEnt), b, out, inherited, sourceId); ++exported; }
        else if (pEnt->isKindOf(AcDbPoint::desc()))
        { EmitPoint(static_cast<AcDbPoint*>(pEnt), b, unitsPerMm, out, inherited, sourceId); ++exported; }
        else if (allowExpansion && pEnt->isKindOf(AcDbBlockReference::desc()))
        { EmitBlockRef(static_cast<AcDbBlockReference*>(pEnt), b, unitsPerMm, out, sourceId, depth, exported, skipped); }
        else if (allowExpansion)
        { EmitExploded(pEnt, b, unitsPerMm, out, inherited, sourceId, depth, exported, skipped); }
        else
        {
            acutPrintf(_T("  [skip] %s (depth limit or unsupported type)\n"), (LPCTSTR)ClassName(pEnt));
            ++skipped;
        }
    }

    // Flattens a nested (block-within-a-block) reference's definition into
    // whichever space `out`/`b` represent, composing the nested reference's
    // own blockTransform on top of whatever transform already got its
    // container there. Used only below the top level — see EmitEntity's
    // comment and EmitTopLevelBlockRef for the top-level, shared-definition
    // path. ByBlock color/lineweight resolve to this reference's own
    // resolved style; note that if this nested reference's own color or
    // lineweight is itself ByBlock, that resolves to a plain default here
    // rather than deferring further up the chain to an eventual currentColor
    // — a known simplification for the rare case of more than one level of
    // ByBlock-referencing-ByBlock nesting.
    //
    // Unlike a top-level block (deduped once into a shared <defs> entry),
    // a nested block is flattened afresh every time it's encountered — the
    // same underlying block definition can be flattened multiple times (two
    // nested references to the same sub-block within one definition, or the
    // same sub-block nested inside two different top-level definitions), so
    // using a sub-entity's own real handle alone as its id would collide
    // across those separate occurrences. Prefixing with `sourceId` — the
    // caller-supplied id already unique to *this occurrence* of `pRef` —
    // keeps every occurrence's ids distinct. This must be the incoming
    // sourceId rather than HandleId(pRef): at nesting depth 2+, `pRef` is
    // itself a transformed clone (made by the parent level's own
    // EmitBlockRef to apply its transform), not a database-resident entity,
    // so it has no real handle of its own — sourceId is what already carried
    // that occurrence's identity down from wherever `pRef` was itself found
    // as a genuine (non-cloned) entity.
    void EmitBlockRef(AcDbBlockReference* pRef, const Bounds* b, double unitsPerMm,
                       LayerMap& out, const CString& sourceId, int depth, int& exported, int& skipped)
    {
        CommonTools::AcDbObjectGuard<AcDbBlockTableRecord> def(pRef->blockTableRecord(), AcDb::kForRead);
        if (!def) { ++skipped; return; }

        AcDbBlockTableRecordIterator* pIter = nullptr;
        if (def->newIterator(pIter) != Acad::eOk) { ++skipped; return; }
        CommonTools::AcDbIteratorGuard<AcDbBlockTableRecordIterator> iterGuard(pIter);

        InheritedStyle refStyle;
        refStyle.color      = ResolveColor(pRef, nullptr);
        refStyle.lineWeight = ResolveLineWeightEnum(pRef, nullptr);
        AcGeMatrix3d xform = pRef->blockTransform();

        for (; !iterGuard->done(); iterGuard->step())
        {
            AcDbEntity* pSub = nullptr;
            if (iterGuard->getEntity(pSub, AcDb::kForRead) != Acad::eOk) continue;

            CString subId;
            subId.Format(_T("%s_%s"), (LPCTSTR)sourceId, (LPCTSTR)HandleId(pSub));
            AcDbEntity* pClone = static_cast<AcDbEntity*>(pSub->clone());
            pSub->close();
            if (!pClone) { ++skipped; continue; }

            pClone->transformBy(xform);
            EmitEntity(pClone, b, unitsPerMm, out, &refStyle, subId, depth + 1, exported, skipped);
            delete pClone;
        }
    }

    // Explodes an unrecognized entity into its displayed representation and
    // emits each resulting piece. Exploded pieces commonly carry a ByBlock
    // color/lineweight inherited from the original object, so the original's
    // own resolved style (given whatever inheritance already applied to it)
    // is threaded through as the pieces' inherited style. Pieces have no
    // handle of their own, so each gets `sourceId` (the exploded entity's own
    // id) with an index suffix.
    void EmitExploded(AcDbEntity* pEnt, const Bounds* b, double unitsPerMm, LayerMap& out,
                       const InheritedStyle* inherited, const CString& sourceId,
                       int depth, int& exported, int& skipped)
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

        InheritedStyle selfStyle;
        selfStyle.color      = ResolveColor(pEnt, inherited);
        selfStyle.lineWeight = ResolveLineWeightEnum(pEnt, inherited);
        for (int i = 0; i < pieces.length(); ++i)
        {
            AcDbEntity* pPiece = static_cast<AcDbEntity*>(pieces[i]);
            if (!pPiece) continue;
            CString pieceId;
            pieceId.Format(_T("%s_%d"), (LPCTSTR)sourceId, i);
            EmitEntity(pPiece, b, unitsPerMm, out, &selfStyle, pieceId, depth + 1, exported, skipped);
            delete pPiece;
        }
    }

    // -------------------------------------------------------------------------
    // Top-level block references: exported as a shared <g id="block_N"> (the
    // block's own geometry, in its own local space, built once per unique
    // AcDbBlockTableRecord) plus one <use href="#block_N" transform="matrix(
    // ...)"> per occurrence — mirroring AutoCAD's own one-definition/many-
    // references data model instead of duplicating each instance's geometry.
    // -------------------------------------------------------------------------

    // Computes the SVG matrix(a,b,c,d,e,f) that places a block instance,
    // derived directly from blockTransform()'s action on the local origin
    // and unit axes — this handles rotation, non-uniform scale, mirroring,
    // and shear uniformly, without assuming any particular decomposition.
    // `topBounds` non-null bakes the selection's Y-flip-and-offset into the
    // translation (the only case actually used today, since nested blocks
    // are flattened rather than further deduped — see EmitBlockRef above);
    // null would place the instance in local-flip-only space instead, for a
    // future nested <use>.
    CString ComputeUseTransform(const AcGeMatrix3d& xform, const Bounds* topBounds)
    {
        AcGePoint3d origin  = AcGePoint3d(0.0, 0.0, 0.0).transformBy(xform);
        AcGePoint3d xAxisPt = AcGePoint3d(1.0, 0.0, 0.0).transformBy(xform);
        AcGePoint3d yAxisPt = AcGePoint3d(0.0, 1.0, 0.0).transformBy(xform);

        double a = xAxisPt.x - origin.x;
        double b = -(xAxisPt.y - origin.y);
        double c = -(yAxisPt.x - origin.x);
        double d = yAxisPt.y - origin.y;
        double e, f;
        if (topBounds) { e = origin.x - topBounds->minX; f = topBounds->maxY - origin.y; }
        else           { e = origin.x;                   f = -origin.y; }

        CString s;
        s.Format(_T("matrix(%.6f,%.6f,%.6f,%.6f,%.6f,%.6f)"), a, b, c, d, e, f);
        return s;
    }

    void EmitTopLevelBlockRef(AcDbBlockReference* pRef, const Bounds& bounds, double unitsPerMm,
                              LayerMap& out, CString& defsBody,
                              std::map<AcDbObjectId, CString>& definedBlocks,
                              int& exported, int& skipped)
    {
        AcDbObjectId defId = pRef->blockTableRecord();
        auto found = definedBlocks.find(defId);
        CString groupId;
        if (found == definedBlocks.end())
        {
            CommonTools::AcDbObjectGuard<AcDbBlockTableRecord> def(defId, AcDb::kForRead);
            if (!def) { ++skipped; return; }

            AcDbBlockTableRecordIterator* pIter = nullptr;
            if (def->newIterator(pIter) != Acad::eOk) { ++skipped; return; }
            CommonTools::AcDbIteratorGuard<AcDbBlockTableRecordIterator> iterGuard(pIter);

            groupId.Format(_T("block_%d"), (int)definedBlocks.size());
            definedBlocks[defId] = groupId;   // register before building, guards a cyclic definition

            LayerMap defLayers;
            int defExported = 0, defSkipped = 0;
            for (; !iterGuard->done(); iterGuard->step())
            {
                AcDbEntity* pSub = nullptr;
                if (iterGuard->getEntity(pSub, AcDb::kForRead) != Acad::eOk) continue;
                // Block-local content: b=nullptr means local-flip-only
                // coordinates and ByBlock color -> currentColor (see Bounds/
                // ColorHex). ByBlock lineweight has no equivalent per-instance
                // CSS override in this implementation, so it resolves to a
                // plain default here, shared by every instance — a known
                // simplification (ByBlock lineweight is far less common in
                // practice than ByBlock color). No clone/transform needed —
                // this is the block's own untransformed local geometry. Each
                // sub-entity's own layer and real handle-based id are
                // preserved, same as top-level content.
                EmitEntity(pSub, nullptr, unitsPerMm, defLayers, nullptr, HandleId(pSub), 1,
                           defExported, defSkipped);
                pSub->close();
            }
            if (defSkipped > 0)
                acutPrintf(_T("  [block %s] %d sub-entit%s skipped\n"),
                           (LPCTSTR)groupId, defSkipped, defSkipped == 1 ? _T("y") : _T("ies"));

            CString g;
            g.Format(_T("  <g id=\"%s\">\n%s  </g>\n"), (LPCTSTR)groupId, (LPCTSTR)WrapLayers(defLayers));
            defsBody += g;
        }
        else
        {
            groupId = found->second;
        }

        CString transform = ComputeUseTransform(pRef->blockTransform(), &bounds);
        AcCmColor col = ResolveColor(pRef, nullptr);

        CString use;
        use.Format(_T("  <use id=\"%s\" href=\"#%s\" transform=\"%s\" color=\"%s\" />\n"),
                   (LPCTSTR)HandleId(pRef), (LPCTSTR)groupId, (LPCTSTR)transform,
                   (LPCTSTR)FormatHex(col.red(), col.green(), col.blue()));
        AppendToLayer(out, pRef, use);
        ++exported;
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

    double width  = (std::max)(bounds.Width(),  1.0);
    double height = (std::max)(bounds.Height(), 1.0);

    // Each entity's actual AutoCAD lineweight (ByLayer/ByBlock resolved, same
    // as color) drives its stroke width; unitsPerMm converts that fixed-in-mm
    // value into the drawing units the exported coordinates are already in.
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    double unitsPerMm = UnitsPerMm(pDb->insunits());

    // Pass 2: emit supported entities, bucketed by layer (see LayerMap).
    // Block references are handled separately (shared <defs> + <use> per
    // instance, itself also placed under its own layer); everything else
    // goes through the general recursive dispatch.
    LayerMap body;
    CString defsBody;
    std::map<AcDbObjectId, CString> definedBlocks;
    int exported = 0, skipped = 0;
    for (auto id : ids)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForRead);
        if (!ent) { ++skipped; continue; }

        if (ent->isKindOf(AcDbBlockReference::desc()))
        {
            EmitTopLevelBlockRef(static_cast<AcDbBlockReference*>(ent.get()), bounds, unitsPerMm,
                                 body, defsBody, definedBlocks, exported, skipped);
        }
        else
        {
            EmitEntity(ent.get(), &bounds, unitsPerMm, body, /*inherited=*/nullptr,
                       HandleId(ent.get()), /*depth=*/0, exported, skipped);
        }
    }

    if (exported == 0)
    {
        acutPrintf(_T("\nNo exportable geometry in the selection — nothing recognized ")
                   _T("directly, and explode() produced nothing for the rest.\n"));
        return;
    }

    // No fixed pixel width/height: those would equal the drawing's real-world
    // unit dimensions (often thousands of mm), which a browser renders at a
    // literal 1:1 pixel scale — usually far larger than the viewport, and
    // often beyond what the browser's own zoom-out range can shrink back down
    // to fit. Leaving only viewBox makes the SVG scale to fill whatever
    // displays it while preserving the drawing's aspect ratio.
    CString svg;
    svg.Format(_T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
               _T("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.4f %.4f\">\n"),
               width, height);
    if (!defsBody.IsEmpty())
    {
        svg += _T("<defs>\n");
        svg += defsBody;
        svg += _T("</defs>\n");
    }
    svg += WrapLayers(body);
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
