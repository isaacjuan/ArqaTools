#include "StdAfx.h"
#include "AreaTools.h"
#include "MeasureFormat.h"
#include "CadInfra.h"
#include "ReactorPersistence.h"
#include "CommonTools.h"
#include "dbapserv.h"
#include "dbpl.h"
#include "dbents.h"
#include "acutads.h"
#include "geassign.h"
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cmath>

// ============================================================================
// CurveTextReactor base class
// ============================================================================
CurveTextReactor::CurveTextReactor(AcDbObjectId curveId, AcDbObjectId labelId)
    : m_curveId(curveId), m_labelId(labelId)
{
    // Transient reactors don't write to disk — kForRead is sufficient and
    // works even when the document hasn't been write-locked yet (e.g. on rebuild).
    CommonTools::AcDbObjectGuard<AcDbObject> obj(m_curveId, AcDb::kForRead);
    if (obj) obj->addReactor(this);
}

CurveTextReactor::~CurveTextReactor()
{
    CommonTools::AcDbObjectGuard<AcDbObject> obj(m_curveId, AcDb::kForRead);
    if (obj) obj->removeReactor(this);
}

void CurveTextReactor::modified(const AcDbObject*) { updateLabel(); }

void CurveTextReactor::erased(const AcDbObject*, Adesk::Boolean bErasing)
{
    if (bErasing)
    {
        CommonTools::AcDbObjectGuard<AcDbObject> lbl(m_labelId, AcDb::kForWrite);
        if (lbl) lbl->erase();
    }
}

// ── Label text and placement, shared by the Insert* cores and the reactors ──
namespace
{
    AcDb::UnitsValue Units() { return acdbHostApplicationServices()->workingDatabase()->insunits(); }

    CString AreaText(double area)        { return MeasureFormat::FormatArea(area, Units()); }
    CString SumLengthText(double length) { return MeasureFormat::FormatLength(length, Units()); }
    CString LengthText(double length)    { return MeasureFormat::FormatLength(length, Units(), true, true); }

    CString PerimeterText(double length)
    {
        CString s;
        s.Format(_T("P: %s"), (LPCTSTR)MeasureFormat::FormatLength(length, Units()));
        return s;
    }

    CString RoomText(const CString& roomName, double area)
    {
        CString s;
        s.Format(_T("%s\\P%s"), (LPCTSTR)roomName, (LPCTSTR)AreaText(area));
        return s;
    }

    // Length label: text at the curve's midpoint, rotated perpendicular to it
    // (kept readable) and offset half a text height to the left of the curve.
    bool LengthLayout(AcDbObjectId curveId, CString& text, AcGePoint3d& pos, double& angle)
    {
        double length = 0.0;
        AcGePoint3d midPt;
        AcGeVector3d perp(0.0, 1.0, 0.0);
        {
            CommonTools::AcDbObjectGuard<AcDbCurve> curve(curveId);
            if (!curve) return false;

            double startParam, endParam;
            curve->getStartParam(startParam);
            curve->getEndParam(endParam);
            CommonTools::CurveLength(curve.get(), length);

            double midParam = (startParam + endParam) * 0.5;
            curve->getPointAtParam(midParam, midPt);

            AcGeVector3d tangent(1.0, 0.0, 0.0);
            curve->getFirstDeriv(midParam, tangent);
            if (tangent.length() > 1e-10) tangent.normalize();

            perp  = AcGeVector3d(-tangent.y, tangent.x, 0.0);
            angle = atan2(perp.y, perp.x);
            if (angle > M_PI / 2.0 || angle <= -M_PI / 2.0) angle += M_PI;
        }

        double textHeight = CadInfra::ResolveTextHeight(acdbHostApplicationServices()->workingDatabase());
        pos  = midPt + perp * (textHeight * 0.5);
        text = LengthText(length);
        return true;
    }

    // Keeps a new label linked to its curve: registers the reactor (so it
    // updates now) and stores the link as xdata (so it is rebuilt on reopen).
    AcDbObjectId LinkLabel(CurveTextReactor* reactor, const TCHAR* appName,
                           const CString* xdataText = nullptr)
    {
        ReactorPersistence::Register(reactor);
        CadInfra::StoreLinkXData(reactor->getCurveId(), reactor->getLabelId(), appName, xdataText);
        return reactor->getLabelId();
    }
}

// ── Concrete reactors: recompute the label from the curve ───────────────────
void PolylineAreaReactor::updateLabel()
{
    double area = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(m_curveId);
        if (!poly || !poly->isClosed() || poly->getArea(area) != Acad::eOk) return;
    }
    CommonTools::AcDbObjectGuard<AcDbText> text(m_labelId, AcDb::kForWrite);
    if (text) text->setTextString(AreaText(area));
}

void PerimeterReactor::updateLabel()
{
    double length = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbCurve> curve(m_curveId);
        if (!curve) return;
        CommonTools::CurveLength(curve.get(), length);
    }
    CommonTools::AcDbObjectGuard<AcDbText> text(m_labelId, AcDb::kForWrite);
    if (text) text->setTextString(PerimeterText(length));
}

void LinearLengthReactor::updateLabel()
{
    CString str;
    AcGePoint3d pos;
    double angle = 0.0;
    if (!LengthLayout(m_curveId, str, pos, angle)) return;

    CommonTools::AcDbObjectGuard<AcDbText> text(m_labelId, AcDb::kForWrite);
    if (!text) return;
    text->setTextString(str);
    text->setPosition(pos);
    text->setAlignmentPoint(pos);
    text->setRotation(angle);
}

void RoomTagReactor::updateLabel()
{
    double area = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(m_curveId);
        if (!poly) return;
        poly->getArea(area);
    }
    CommonTools::AcDbObjectGuard<AcDbMText> mtext(m_labelId, AcDb::kForWrite);
    if (mtext) mtext->setContents(RoomText(m_roomName, area));
}

// ============================================================================
// PolylineSumLengthReactor
// ============================================================================
PolylineSumLengthReactor::PolylineSumLengthReactor(
    const std::vector<AcDbObjectId>& polylineIds, AcDbObjectId textId)
    : m_polylineIds(polylineIds), m_textId(textId)
{
    for (const auto& id : m_polylineIds)
    {
        CommonTools::AcDbObjectGuard<AcDbObject> obj(id, AcDb::kForRead);
        if (obj) obj->addReactor(this);
    }
    CommonTools::AcDbObjectGuard<AcDbObject> t(textId, AcDb::kForRead);
    if (t) t->addReactor(this);
}

PolylineSumLengthReactor::~PolylineSumLengthReactor()
{
    for (const auto& id : m_polylineIds)
    {
        CommonTools::AcDbObjectGuard<AcDbObject> obj(id, AcDb::kForRead);
        if (obj) obj->removeReactor(this);
    }
    CommonTools::AcDbObjectGuard<AcDbObject> t(m_textId, AcDb::kForRead);
    if (t) t->removeReactor(this);
}

void PolylineSumLengthReactor::modified(const AcDbObject*) { updateSumLengthText(); }

void PolylineSumLengthReactor::erased(const AcDbObject* pObj, Adesk::Boolean bErasing)
{
    if (!bErasing) return;
    removePolylineFromList(pObj->objectId());
    if (m_polylineIds.empty())
    {
        CommonTools::AcDbObjectGuard<AcDbObject> t(m_textId, AcDb::kForWrite);
        if (t) t->erase();
    }
    else updateSumLengthText();
}

void PolylineSumLengthReactor::highlighted(const AcDbEntity* pEnt,
                                            Adesk::Boolean bHighlight)
{
    if (pEnt->objectId() == m_textId)
        highlightLinkedCurves(bHighlight ? true : false);
}

void PolylineSumLengthReactor::updateSumLengthText()
{
    double total = 0.0;
    for (const auto& id : m_polylineIds)
    {
        CommonTools::AcDbObjectGuard<AcDbCurve> curve(id);
        if (!curve) continue;
        double len = 0.0;
        if (CommonTools::CurveLength(curve.get(), len))
            total += len;
    }
    CommonTools::AcDbObjectGuard<AcDbText> text(m_textId, AcDb::kForWrite);
    if (text) text->setTextString(SumLengthText(total));
}

void PolylineSumLengthReactor::removePolylineFromList(AcDbObjectId id)
{
    auto it = std::find(m_polylineIds.begin(), m_polylineIds.end(), id);
    if (it != m_polylineIds.end()) m_polylineIds.erase(it);
}

void PolylineSumLengthReactor::highlightLinkedCurves(bool on)
{
    for (const auto& id : m_polylineIds)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
        if (ent) ent->setColorIndex(on ? 4 : 256);
    }
}

// ============================================================================
// INSERTAREA - Insert auto-updating area text in a closed polyline
// ============================================================================
void insertAreaCommand()
{
    acutPrintf(_T("\nINSERTAREA - Insert area value in closed polyline"));

    ads_name ent; ads_point pt;
    if (acedEntSel(_T("\nSelect a closed polyline: "), ent, pt) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    AcDbObjectId polylineId;
    acdbGetObjectId(polylineId, ent);

    CString err;
    AcDbObjectId textId = AreaTools::InsertAreaLabel(polylineId, &err);
    if (textId.isNull())
    { acutPrintf(_T("\nError: %s."), (LPCTSTR)err); return; }

    CString areaText;
    { CommonTools::AcDbObjectGuard<AcDbText> t(textId); if (t) areaText = t->textStringConst(); }
    acutPrintf(_T("\nArea text inserted: %s"), (LPCTSTR)areaText);
}

AcDbObjectId AreaTools::InsertAreaLabel(AcDbObjectId polylineId, CString* err)
{
    auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

    AcGePoint3d centroid;
    double area = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(polylineId);
        if (!poly) return fail(_T("object is not a polyline"));
        if (!poly->isClosed()) return fail(_T("polyline must be closed"));
        if (poly->getArea(area) != Acad::eOk) return fail(_T("could not calculate area"));
        if (!CadInfra::GetPolylineCentroid(poly.get(), centroid))
            return fail(_T("could not calculate centroid"));
    }

    AcDbObjectId textId = CadInfra::InsertText(centroid, AreaText(area));
    if (textId.isNull()) return fail(_T("could not add text to drawing"));

    return LinkLabel(new PolylineAreaReactor(polylineId, textId), CadInfra::AREA_APP_NAME);
}

// ============================================================================
// SUMLENGTH - Insert auto-updating sum-of-lengths text
// ============================================================================

// Accumulate curve lengths from a list of ids (non-curves are skipped).
static bool CollectCurveLengths(const std::vector<AcDbObjectId>& candidates,
                                 std::vector<AcDbObjectId>& ids, double& totalLength)
{
    for (AcDbObjectId objId : candidates)
    {
        CommonTools::AcDbObjectGuard<AcDbCurve> curve(objId);
        double len = 0.0;
        if (curve && CommonTools::CurveLength(curve.get(), len))
        {
            totalLength += len;
            ids.push_back(objId);
        }
    }
    return !ids.empty();
}

AcDbObjectId AreaTools::InsertSumLengthLabel(const std::vector<AcDbObjectId>& curveIds,
                                             const AcGePoint3d& pos, double* totalOut,
                                             int* monitoredOut, CString* err)
{
    auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

    std::vector<AcDbObjectId> ids;
    double total = 0.0;
    if (!CollectCurveLengths(curveIds, ids, total))
        return fail(_T("no valid curves"));

    AcDbObjectId textId = CadInfra::InsertText(pos, SumLengthText(total));
    if (textId.isNull()) return fail(_T("could not add text to drawing"));

    ReactorPersistence::Register(new PolylineSumLengthReactor(ids, textId));
    for (const auto& id : ids)
        CadInfra::StoreLinkXData(id, textId, CadInfra::SUM_APP_NAME);

    if (totalOut)     *totalOut = total;
    if (monitoredOut) *monitoredOut = static_cast<int>(ids.size());
    return textId;
}

AcDbObjectId AreaTools::InsertRoomTag(AcDbObjectId polyId, const CString& roomName, CString* err)
{
    auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

    AcGePoint3d centroid;
    double area = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(polyId);
        if (!poly) return fail(_T("object is not a polyline"));
        if (!poly->isClosed()) return fail(_T("polyline must be closed"));
        poly->getArea(area);
        CadInfra::GetPolylineCentroid(poly.get(), centroid);
    }

    AcDbObjectId mtextId = CadInfra::InsertMText(centroid, RoomText(roomName, area));
    if (mtextId.isNull()) return fail(_T("could not insert room tag"));

    return LinkLabel(new RoomTagReactor(polyId, mtextId, roomName), CadInfra::ROOM_APP_NAME, &roomName);
}

AcDbObjectId AreaTools::InsertPerimeterLabel(AcDbObjectId polyId, CString* err)
{
    auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return AcDbObjectId::kNull; };

    AcGePoint3d centroid;
    double length = 0.0;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> poly(polyId);
        if (!poly) return fail(_T("object is not a polyline"));
        if (!poly->isClosed()) return fail(_T("polyline must be closed"));
        CommonTools::CurveLength(poly.get(), length);
        CadInfra::GetPolylineCentroid(poly.get(), centroid);
    }

    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    // 1.5 text heights below the centroid (the height rule already applies
    // the style height and the per-unit minimum).
    double offset = CadInfra::ResolveTextHeight(pDb) * 1.5;
    AcGePoint3d textPos(centroid.x, centroid.y - offset, centroid.z);

    AcDbObjectId textId = CadInfra::InsertText(textPos, PerimeterText(length));
    if (textId.isNull()) return fail(_T("could not insert text"));

    return LinkLabel(new PerimeterReactor(polyId, textId), CadInfra::PERIM_APP_NAME);
}

// ============================================================================
// LINEARLENGTH / TAGALL helper
// Insert a perpendicular length label on a single curve + attach reactor.
// ============================================================================
AcDbObjectId AreaTools::InsertLengthLabel(AcDbObjectId curveId, const CString& layerName, CString* err)
{
    CString str;
    AcGePoint3d pos;
    double angle = 0.0;
    if (!LengthLayout(curveId, str, pos, angle))
    { if (err) *err = _T("object is not a curve"); return AcDbObjectId::kNull; }

    // Left+base alignment so text grows away from the segment, not over it.
    AcDbObjectId textId = CadInfra::InsertText(pos, str, angle, AcDb::kTextLeft, AcDb::kTextBase, layerName);
    if (textId.isNull())
    { if (err) *err = _T("could not add text to drawing"); return AcDbObjectId::kNull; }

    return LinkLabel(new LinearLengthReactor(curveId, textId), CadInfra::LL_APP_NAME);
}

// ============================================================================
// COUNTBLOCKS - Count block instances in selection or whole drawing
// ============================================================================
void countBlocksCommand()
{
    acutPrintf(_T("\nCOUNTBLOCKS - Count block instances in drawing\n"));

    TCHAR optBuf[32] = _T("S");
    int result = acedGetString(0, _T("\nCount in [S]election or [D]rawing? <S>: "), optBuf);
    if (result != RTNORM && result != RTNONE)
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    CString opt(optBuf); opt.MakeUpper();
    bool wholeDrawing = (opt == _T("D"));

    std::map<CString, int> blockCount;
    if (wholeDrawing)
        blockCount = AreaTools::CountBlocks(nullptr);
    else
    {
        std::vector<AcDbObjectId> ids = CommonTools::SelectIds();
        if (ids.empty()) { acutPrintf(_T("\nNo objects selected.")); return; }
        blockCount = AreaTools::CountBlocks(&ids);
    }

    if (blockCount.empty()) { acutPrintf(_T("\nNo block references found.")); return; }

    acutPrintf(_T("\n%-40s  COUNT\n"), _T("BLOCK NAME"));
    acutPrintf(_T("----------------------------------------  -----\n"));
    int total = 0;
    for (auto& kv : blockCount)
    { acutPrintf(_T("%-40s  %d\n"), (LPCTSTR)kv.first, kv.second); total += kv.second; }
    acutPrintf(_T("----------------------------------------  -----\n"));
    acutPrintf(_T("%-40s  %d\n"), _T("TOTAL"), total);
}

std::map<CString, int> AreaTools::CountBlocks(const std::vector<AcDbObjectId>* ids)
{
    std::map<CString, int> blockCount;
    auto countEntity = [&](AcDbObjectId id)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
        if (!ent || !ent->isKindOf(AcDbBlockReference::desc())) return;
        CString name = CommonTools::BlockName(static_cast<AcDbBlockReference*>(ent.get()));
        if (!name.IsEmpty() && name[0] != _T('*'))
            blockCount[name]++;
    };

    if (ids)
    {
        for (AcDbObjectId id : *ids) countEntity(id);
        return blockCount;
    }

    std::vector<AcDbObjectId> all = CommonTools::ModelSpaceIds();
    for (AcDbObjectId id : all) countEntity(id);
    return blockCount;
}

// ============================================================================
// Shared geometry: collect intersections of pBase with a list of entities
// ============================================================================
static void CollectIntersectionPoints(AcDbCurve* pBase, AcDbObjectId baseId,
                                       const std::vector<AcDbObjectId>& crossIds,
                                       std::vector<AcGePoint3d>& pts)
{
    for (AcDbObjectId crossId : crossIds)
    {
        if (crossId == baseId) continue;
        CommonTools::AcDbObjectGuard<AcDbEntity> cross(crossId);
        if (!cross) continue;
        AcGePoint3dArray intPts;
        pBase->intersectWith(cross.get(), AcDb::kOnBothOperands, intPts);
        for (int j = 0; j < intPts.length(); j++)
            pts.push_back(intPts[j]);
    }
}

// Prompts for the crossing entities (shared by SPLITLINE / SPLITPOLI).
static bool SelectCrossingEntities(std::vector<AcDbObjectId>& crossIds)
{
    acutPrintf(_T("\nSelect crossing lines/polylines: "));
    crossIds = CommonTools::SelectIds();
    return !crossIds.empty();
}

// ============================================================================
// SPLITLINE - Split a line at intersections, creating tagged AcDbLine segments
// ============================================================================
void splitLineCommand()
{
    acutPrintf(_T("\nSPLITLINE - Create segments from a line intersected by other lines"));

    ads_name baseEnt; ads_point basePt;
    if (acedEntSel(_T("\nSelect base line to split: "), baseEnt, basePt) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    AcDbObjectId baseId;
    acdbGetObjectId(baseId, baseEnt);
    {
        CommonTools::AcDbObjectGuard<AcDbCurve> base(baseId);
        if (!base) { acutPrintf(_T("\nError: Cannot open base line.")); return; }
    }

    std::vector<AcDbObjectId> crossIds;
    if (!SelectCrossingEntities(crossIds))
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    CString err;
    std::vector<AcDbObjectId> segIds = AreaTools::SplitLine(baseId, crossIds, _T("doc_areas"), true, &err);
    if (segIds.empty())
    { acutPrintf(_T("\n%s."), (LPCTSTR)err); return; }

    acutPrintf(_T("\n%d segment(s) created and tagged."), static_cast<int>(segIds.size()));
}

std::vector<AcDbObjectId> AreaTools::SplitLine(AcDbObjectId baseId,
                                               const std::vector<AcDbObjectId>& crossIds,
                                               const CString& targetLayer, bool tagLengths,
                                               CString* err)
{
    std::vector<AcDbObjectId> segIds;
    auto fail = [&](const TCHAR* msg) { if (err) *err = msg; return segIds; };

    AcGePoint3d startPt, endPt;
    std::vector<AcGePoint3d> cleanPts;
    {
        CommonTools::AcDbObjectGuard<AcDbCurve> base(baseId);
        if (!base) return fail(_T("cannot open base line"));
        base->getStartPoint(startPt);
        base->getEndPoint(endPt);

        std::vector<AcGePoint3d> pts = { startPt, endPt };
        CollectIntersectionPoints(base.get(), baseId, crossIds, pts);

        AcGeVector3d dir = endPt - startPt;
        std::sort(pts.begin(), pts.end(), [&](const AcGePoint3d& a, const AcGePoint3d& b)
            { return dir.dotProduct(a - startPt) < dir.dotProduct(b - startPt); });

        const double tol = 0.1;
        cleanPts.push_back(pts[0]);
        for (size_t i = 1; i < pts.size(); i++)
            if (pts[i].distanceTo(cleanPts.back()) > tol)
                cleanPts.push_back(pts[i]);
    }

    if (cleanPts.size() < 2)
        return fail(_T("no valid intersections found"));

    if (!targetLayer.IsEmpty()) CadInfra::EnsureLayer(targetLayer);

    AcDbBlockTableRecord* pBTR = nullptr;
    if (CommonTools::GetModelSpace(pBTR) != Acad::eOk)
        return fail(_T("could not open model space"));

    for (size_t i = 0; i + 1 < cleanPts.size(); i++)
    {
        AcDbLine* pLine = new AcDbLine(cleanPts[i], cleanPts[i + 1]);
        if (!targetLayer.IsEmpty()) pLine->setLayer(targetLayer);
        AcDbObjectId lineId = CommonTools::AppendEntity(pBTR, pLine);
        if (!lineId.isNull()) segIds.push_back(lineId);
    }
    pBTR->close();

    if (tagLengths)
        for (auto& id : segIds) InsertLengthLabel(id, targetLayer);

    { CommonTools::AcDbObjectGuard<AcDbEntity> orig(baseId, AcDb::kForWrite); if (orig) orig->erase(); }
    return segIds;
}

// ============================================================================
// SPLITPOLI - Split a polyline at intersections, preserving arc segments
// ============================================================================

// Derive arc center from chord endpoints and bulge (positive = CCW).
static AcGePoint2d GetArcCenter(const AcGePoint2d& S, const AcGePoint2d& E, double bulge)
{
    AcGePoint2d M((S.x + E.x) * 0.5, (S.y + E.y) * 0.5);
    double chord = S.distanceTo(E);
    if (chord < 1e-10) return M;
    double a     = chord * 0.5;
    double theta = 4.0 * atan(fabs(bulge));
    double r     = a / sin(theta * 0.5);
    double d_mc  = sqrt(fabs(r * r - a * a));
    // Left-perpendicular unit vector of S→E (CCW bulge → center to the left)
    double px = -(E.y - S.y) / chord;
    double py =  (E.x - S.x) / chord;
    double sign = (bulge > 0) ? 1.0 : -1.0;
    return AcGePoint2d(M.x + sign * d_mc * px, M.y + sign * d_mc * py);
}

// Compute bulge for the trimmed arc fromPt→toPt on the same circle.
static double ComputePartialBulge(const AcGePoint2d& center,
                                   const AcGePoint2d& fromPt,
                                   const AcGePoint2d& toPt,
                                   bool clockwise)
{
    double a1    = atan2(fromPt.y - center.y, fromPt.x - center.x);
    double a2    = atan2(toPt.y   - center.y, toPt.x   - center.x);
    double sweep = clockwise ? (a1 - a2) : (a2 - a1);
    while (sweep <= 0.0)         sweep += 2.0 * M_PI;
    while (sweep >  2.0 * M_PI) sweep -= 2.0 * M_PI;
    double b = tan(sweep / 4.0);
    return clockwise ? -b : b;
}

// Return bulge for the sub-arc [fromPt, toPt] within polyline segment seg.
static double BulgeForSegment(AcDbPolyline* pPoly, int seg,
                               const AcGePoint2d& fromPt, const AcGePoint2d& toPt)
{
    double origBulge = 0.0;
    pPoly->getBulgeAt(seg, origBulge);
    if (fabs(origBulge) < 1e-10) return 0.0;
    AcGePoint2d S, E;
    pPoly->getPointAt(seg,     S);
    pPoly->getPointAt(seg + 1, E);
    return ComputePartialBulge(GetArcCenter(S, E, origBulge), fromPt, toPt, origBulge < 0);
}

// Build a new polyline covering parameter range [p1, p2] of pPoly, preserving arcs.
static AcDbPolyline* ExtractSubPolyline(AcDbPolyline* pPoly,
                                         double p1, const AcGePoint3d& pt1,
                                         double p2, const AcGePoint3d& pt2)
{
    const double vTol = 1e-9;
    int nVerts = (int)pPoly->numVerts();

    AcDbPolyline* pNew = new AcDbPolyline();
    pNew->setNormal(pPoly->normal());
    pNew->setElevation(pPoly->elevation());

    bool p1AtVertex = (fabs(p1 - floor(p1)) < vTol);
    bool p2AtVertex = (fabs(p2 - floor(p2)) < vTol);
    int  seg1  = (int)floor(p1);
    int  iFirst = seg1 + 1;
    int  iLast  = p2AtVertex ? (int)round(p2) : (int)floor(p2);

    AcGePoint2d apt1(pt1.x, pt1.y), apt2(pt2.x, pt2.y);

    // Both endpoints within the same segment
    if (iFirst > iLast && !p2AtVertex)
    {
        pNew->addVertexAt(0, apt1, BulgeForSegment(pPoly, seg1, apt1, apt2));
        pNew->addVertexAt(1, apt2);
        return pNew;
    }

    // First vertex
    double startBulge = 0.0;
    if (p1AtVertex)
    { if (seg1 < nVerts) pPoly->getBulgeAt(seg1, startBulge); }
    else
    {
        AcGePoint2d vEnd;
        if (seg1 + 1 < nVerts)
        { pPoly->getPointAt(seg1 + 1, vEnd); startBulge = BulgeForSegment(pPoly, seg1, apt1, vEnd); }
    }
    pNew->addVertexAt(0, apt1, startBulge);

    // Intermediate full vertices
    for (int i = iFirst; i <= iLast && i < nVerts; i++)
    {
        AcGePoint2d vpt; pPoly->getPointAt(i, vpt);
        double bulge = 0.0; pPoly->getBulgeAt(i, bulge);
        if (i == iLast && !p2AtVertex)
            bulge = BulgeForSegment(pPoly, i, vpt, apt2);
        pNew->addVertexAt(pNew->numVerts(), vpt, bulge);
    }

    // End vertex (if not already at exact vertex)
    if (!p2AtVertex)
    {
        AcGePoint2d lastPt;
        pNew->getPointAt(pNew->numVerts() - 1, lastPt);
        if (apt2.distanceTo(lastPt) > vTol)
            pNew->addVertexAt(pNew->numVerts(), apt2);
    }

    return pNew;
}

void splitPoliCommand()
{
    acutPrintf(_T("\nSPLITPOLI - Split a polyline at intersections with crossing entities"));

    ads_name baseEnt; ads_point basePt;
    if (acedEntSel(_T("\nSelect base polyline to split: "), baseEnt, basePt) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    AcDbObjectId baseId;
    acdbGetObjectId(baseId, baseEnt);
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> base(baseId);
        if (!base) { acutPrintf(_T("\nError: Selected object is not a polyline.")); return; }
    }

    std::vector<AcDbObjectId> crossIds;
    if (!SelectCrossingEntities(crossIds))
    { acutPrintf(_T("\nCommand cancelled.")); return; }

    CString err;
    std::vector<AcDbObjectId> segIds = AreaTools::SplitPolyline(baseId, crossIds, _T("doc_areas"), true, &err);
    if (segIds.empty())
    { acutPrintf(_T("\n%s."), (LPCTSTR)err); return; }

    acutPrintf(_T("\n%d segment(s) created and tagged."), static_cast<int>(segIds.size()));
}

std::vector<AcDbObjectId> AreaTools::SplitPolyline(AcDbObjectId baseId,
                                                   const std::vector<AcDbObjectId>& crossIds,
                                                   const CString& targetLayer, bool tagLengths,
                                                   CString* err)
{
    std::vector<AcDbObjectId> segIds;
    auto fail = [&](const TCHAR* msg) { if (err) *err = msg; return segIds; };

    std::vector<AcGePoint3d> rawPts;
    std::vector<double>      rawParams;
    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> base(baseId);
        if (!base) return fail(_T("object is not a polyline"));

        AcGePoint3d sp, ep;
        base->getStartPoint(sp); base->getEndPoint(ep);
        rawPts.push_back(sp); rawPts.push_back(ep);

        CollectIntersectionPoints(base.get(), baseId, crossIds, rawPts);

        for (const auto& pt : rawPts)
        { double p = -1.0; base->getParamAtPoint(pt, p); rawParams.push_back(p); }
    }

    std::vector<std::pair<double, AcGePoint3d>> paramPts;
    for (size_t i = 0; i < rawPts.size() && i < rawParams.size(); i++)
        if (rawParams[i] >= 0.0)
            paramPts.push_back({ rawParams[i], rawPts[i] });

    if (paramPts.size() < 2)
        return fail(_T("no usable intersection points found"));

    std::sort(paramPts.begin(), paramPts.end(),
        [](const auto& a, const auto& b){ return a.first < b.first; });

    const double tol = 0.1;
    std::vector<std::pair<double, AcGePoint3d>> cleanPts;
    cleanPts.push_back(paramPts[0]);
    for (size_t i = 1; i < paramPts.size(); i++)
        if (paramPts[i].second.distanceTo(cleanPts.back().second) > tol)
            cleanPts.push_back(paramPts[i]);

    if (cleanPts.size() < 2)
        return fail(_T("not enough distinct intersection points"));

    if (!targetLayer.IsEmpty()) CadInfra::EnsureLayer(targetLayer);

    AcDbBlockTableRecord* pBTR = nullptr;
    if (CommonTools::GetModelSpace(pBTR) != Acad::eOk)
        return fail(_T("could not open model space"));

    {
        CommonTools::AcDbObjectGuard<AcDbPolyline> base(baseId);
        if (!base) { pBTR->close(); return fail(_T("object is not a polyline")); }
        for (size_t i = 0; i + 1 < cleanPts.size(); i++)
        {
            AcDbPolyline* pSeg = ExtractSubPolyline(base.get(),
                cleanPts[i].first,   cleanPts[i].second,
                cleanPts[i+1].first, cleanPts[i+1].second);
            if (!pSeg || pSeg->numVerts() < 2) { delete pSeg; continue; }
            if (!targetLayer.IsEmpty()) pSeg->setLayer(targetLayer);
            AcDbObjectId segId = CommonTools::AppendEntity(pBTR, pSeg);
            if (!segId.isNull()) segIds.push_back(segId);
        }
    }
    pBTR->close();

    if (tagLengths)
        for (auto& id : segIds) InsertLengthLabel(id, targetLayer);

    { CommonTools::AcDbObjectGuard<AcDbEntity> orig(baseId, AcDb::kForWrite); if (orig) orig->erase(); }
    return segIds;
}

// ============================================================================
// TAGALL - Batch tag all selected lines/polylines with length text
// ============================================================================
void tagAllCommand()
{
    acutPrintf(_T("\nTAGALL - Insert length text on all selected lines/polylines"));

    std::vector<AcDbObjectId> ids = CommonTools::SelectIds();
    if (ids.empty()) { acutPrintf(_T("\nNo objects selected.")); return; }

    int tagged = 0, skipped = 0;
    for (AcDbObjectId id : ids)
    {
        bool isCurve = false;
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
            if (!ent) continue;
            isCurve = ent->isKindOf(AcDbCurve::desc());
        }
        if (isCurve && !AreaTools::InsertLengthLabel(id).isNull()) tagged++;
        else skipped++;
    }
    acutPrintf(_T("\nTAGALL complete: %d tagged, %d skipped."), tagged, skipped);
}

// ============================================================================
// Persistence lifecycle — delegates to ReactorPersistence
// ============================================================================
void InitAreaToolsPersistence()   { ReactorPersistence::Init();   }
void UninitAreaToolsPersistence() { ReactorPersistence::Uninit(); }
