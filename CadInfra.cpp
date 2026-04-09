#include "StdAfx.h"
#include "CadInfra.h"
#include "MeasureFormat.h"
#include "CommonTools.h"
#include "dbapserv.h"
#include "dbpl.h"
#include "dbents.h"
#include "acutads.h"
#include "geassign.h"
#include <cmath>
#include <algorithm>

namespace CadInfra
{

// ── App name constants ───────────────────────────────────────────────────────
const TCHAR* const AREA_APP_NAME  = _T("ARQATOOLS_AREA");
const TCHAR* const PERIM_APP_NAME = _T("ARQATOOLS_PERIM");
const TCHAR* const ROOM_APP_NAME  = _T("ARQATOOLS_ROOM");
const TCHAR* const SUM_APP_NAME   = _T("ARQATOOLS_SUM");
const TCHAR* const LL_APP_NAME    = _T("ARQATOOLS_LL");

// ── Color / lineweight utilities ─────────────────────────────────────────────

bool ParseHexColor(const std::string& s, int& r, int& g, int& b)
{
    if (s.size() != 7 || s[0] != '#') return false;
    auto hx = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    };
    auto pair = [&](int p) -> int {
        int hi = hx(s[p]), lo = hx(s[p + 1]);
        return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
    };
    r = pair(1); g = pair(3); b = pair(5);
    return (r >= 0 && g >= 0 && b >= 0);
}

bool NamedColorToRGB(const std::string& name, int& r, int& g, int& b)
{
    static const struct { const char* name; int r, g, b; } kColors[] = {
        { "red",     255,   0,   0 }, { "green",     0, 128,   0 },
        { "blue",      0,   0, 255 }, { "yellow",  255, 255,   0 },
        { "cyan",      0, 255, 255 }, { "magenta", 255,   0, 255 },
        { "white",   255, 255, 255 }, { "black",     0,   0,   0 },
        { "gray",    128, 128, 128 }, { "grey",    128, 128, 128 },
        { "orange",  255, 165,   0 }, { "purple",  128,   0, 128 },
        { "brown",   165,  42,  42 },
    };
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& e : kColors)
        if (lower == e.name) { r = e.r; g = e.g; b = e.b; return true; }
    return false;
}

AcDb::LineWeight MmToLineWeight(double mm)
{
    static const struct { double mm; AcDb::LineWeight lw; } kWeights[] = {
        { 0.00, AcDb::kLnWt000 }, { 0.05, AcDb::kLnWt005 },
        { 0.09, AcDb::kLnWt009 }, { 0.13, AcDb::kLnWt013 },
        { 0.15, AcDb::kLnWt015 }, { 0.18, AcDb::kLnWt018 },
        { 0.20, AcDb::kLnWt020 }, { 0.25, AcDb::kLnWt025 },
        { 0.30, AcDb::kLnWt030 }, { 0.35, AcDb::kLnWt035 },
        { 0.40, AcDb::kLnWt040 }, { 0.50, AcDb::kLnWt050 },
        { 0.53, AcDb::kLnWt053 }, { 0.60, AcDb::kLnWt060 },
        { 0.70, AcDb::kLnWt070 }, { 0.80, AcDb::kLnWt080 },
        { 0.90, AcDb::kLnWt090 }, { 1.00, AcDb::kLnWt100 },
        { 1.06, AcDb::kLnWt106 }, { 1.20, AcDb::kLnWt120 },
        { 1.40, AcDb::kLnWt140 }, { 1.58, AcDb::kLnWt158 },
        { 2.00, AcDb::kLnWt200 }, { 2.11, AcDb::kLnWt211 },
    };
    AcDb::LineWeight best = AcDb::kLnWt025;
    double bestDiff = 1e9;
    for (const auto& e : kWeights)
    {
        double d = std::abs(e.mm - mm);
        if (d < bestDiff) { bestDiff = d; best = e.lw; }
    }
    return best;
}

// ── Layer ────────────────────────────────────────────────────────────────────

void EnsureLayer(const CString& name, const LayerProps& props)
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    AcDbLayerTable* pLT = nullptr;
    if (pDb->getLayerTable(pLT, AcDb::kForWrite) != Acad::eOk) return;

    if (!pLT->has(name))
    {
        AcDbLayerTableRecord* pRec = new AcDbLayerTableRecord();
        pRec->setName(name);

        // ── Color ────────────────────────────────────────────────────────────
        if (!props.color.empty())
        {
            int r = 0, g = 0, b = 0;
            if (ParseHexColor(props.color, r, g, b) || NamedColorToRGB(props.color, r, g, b))
            {
                AcCmColor c;
                c.setRGB(static_cast<Adesk::UInt8>(r),
                         static_cast<Adesk::UInt8>(g),
                         static_cast<Adesk::UInt8>(b));
                pRec->setColor(c);
            }
        }

        // ── Linetype ─────────────────────────────────────────────────────────
        if (!props.linetype.empty())
        {
            CA2T wLt(props.linetype.c_str(), CP_UTF8);
            EnsureLinetype(CString(wLt));
            AcDbLinetypeTable* pLtT = nullptr;
            if (pDb->getLinetypeTable(pLtT, AcDb::kForRead) == Acad::eOk)
            {
                AcDbObjectId ltId;
                if (pLtT->getAt(CString(wLt), ltId) == Acad::eOk)
                    pRec->setLinetypeObjectId(ltId);
                pLtT->close();
            }
        }

        // ── Lineweight ───────────────────────────────────────────────────────
        if (props.lineweight >= 0.0)
            pRec->setLineWeight(MmToLineWeight(props.lineweight));

        // ── Description ──────────────────────────────────────────────────────
        if (!props.description.empty())
        {
            CA2T wDesc(props.description.c_str(), CP_UTF8);
            pRec->setDescription(CString(wDesc));
        }

        // ── Plot / locked ────────────────────────────────────────────────────
        pRec->setIsPlottable(props.plot);
        pRec->setIsLocked(props.locked);

        pLT->add(pRec);
        pRec->close();
    }
    pLT->close();
}

// ── Linetype ─────────────────────────────────────────────────────────────────
void EnsureLinetype(const CString& name)
{
    // "Continuous" / "ByLayer" / "ByBlock" are always available — skip loading.
    CString upper = name;
    upper.MakeUpper();
    if (upper == _T("CONTINUOUS") || upper == _T("BYLAYER") || upper == _T("BYBLOCK"))
        return;

    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    AcDbLinetypeTable* pLT = nullptr;
    if (pDb->getLinetypeTable(pLT, AcDb::kForWrite) != Acad::eOk) return;

    if (!pLT->has(name))
    {
        // Try the standard AutoCAD linetype files (metric first, then imperial).
        static const TCHAR* ltFiles[] = { _T("acadiso.lin"), _T("acad.lin") };
        for (auto* ltFile : ltFiles)
        {
            if (pDb->loadLineTypeFile(name, ltFile) == Acad::eOk)
                break;
        }
    }
    pLT->close();
}

// ── Entity insertion ─────────────────────────────────────────────────────────

// Internal: append any new entity to model space and return its ObjectId.
static AcDbObjectId AppendToModelSpace(AcDbEntity* pEnt)
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    AcDbBlockTable* pBT = nullptr;
    if (pDb->getBlockTable(pBT, AcDb::kForRead) != Acad::eOk)
    { delete pEnt; return AcDbObjectId::kNull; }

    AcDbBlockTableRecord* pBTR = nullptr;
    if (pBT->getAt(ACDB_MODEL_SPACE, pBTR, AcDb::kForWrite) != Acad::eOk)
    { pBT->close(); delete pEnt; return AcDbObjectId::kNull; }
    pBT->close();

    AcDbObjectId id;
    if (pBTR->appendAcDbEntity(id, pEnt) != Acad::eOk)
    { pBTR->close(); delete pEnt; return AcDbObjectId::kNull; }

    pEnt->close();
    pBTR->close();
    return id;
}

double ResolveTextHeight(AcDbDatabase* pDb)
{
    double h    = pDb->textsize();
    double minH = (pDb->insunits() == AcDb::kUnitsMillimeters) ? 100.0 : 2.5;

    // If the active style has a fixed size, prefer it.
    AcDbTextStyleTable* pST = nullptr;
    if (pDb->getTextStyleTable(pST, AcDb::kForRead) == Acad::eOk)
    {
        CommonTools::AcDbObjectGuard<AcDbTextStyleTableRecord>
            rec(pDb->textstyle());
        if (rec)
        {
            double sh = rec->textSize();
            if (sh > 0.0) h = sh;
        }
        pST->close();
    }
    return (h < minH) ? minH : h;
}

AcDbObjectId InsertText(const AcGePoint3d& pos, const CString& str,
                        double rotation, const CString& layerName)
{
    return InsertText(pos, str, rotation,
                      AcDb::kTextCenter, AcDb::kTextVertMid, layerName);
}

AcDbObjectId InsertText(const AcGePoint3d& pos, const CString& str,
                        double rotation,
                        AcDb::TextHorzMode horzMode,
                        AcDb::TextVertMode  vertMode,
                        const CString& layerName)
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    double h = ResolveTextHeight(pDb);

    AcDbText* pText = new AcDbText();
    pText->setPosition(pos);
    pText->setAlignmentPoint(pos);
    pText->setTextString(str);
    pText->setHeight(h);
    pText->setRotation(rotation);
    pText->setHorizontalMode(horzMode);
    pText->setVerticalMode(vertMode);
    pText->setTextStyle(pDb->textstyle());
    if (!layerName.IsEmpty()) pText->setLayer(layerName);

    return AppendToModelSpace(pText);
}

AcDbObjectId InsertMText(const AcGePoint3d& pos, const CString& str)
{
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    double h = ResolveTextHeight(pDb);

    AcDbMText* pMText = new AcDbMText();
    pMText->setLocation(pos);
    pMText->setTextHeight(h);
    pMText->setAttachment(AcDbMText::kMiddleCenter);
    pMText->setContents(str);
    pMText->setTextStyle(pDb->textstyle());

    return AppendToModelSpace(pMText);
}

// ── Geometry helpers ─────────────────────────────────────────────────────────
bool GetPolylineCentroid(AcDbPolyline* pPoly, AcGePoint3d& centroid)
{
    if (!pPoly || !pPoly->isClosed()) return false;

    AcDbExtents ext;
    if (pPoly->getGeomExtents(ext) != Acad::eOk) return false;

    centroid = ext.minPoint() + (ext.maxPoint() - ext.minPoint()) * 0.5;
    return true;
}

bool UpdateAreaText(AcDbObjectId polylineId, AcDbObjectId textId)
{
    CommonTools::AcDbObjectGuard<AcDbPolyline> poly(polylineId);
    if (!poly) return false;
    if (!poly->isClosed()) return false;

    double area = 0.0;
    if (poly->getArea(area) != Acad::eOk) return false;

    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    CString areaText = MeasureFormat::FormatArea(area, pDb->insunits());

    CommonTools::AcDbObjectGuard<AcDbText> text(textId, AcDb::kForWrite);
    if (!text) return false;
    text->setTextString(areaText);
    return true;
}

// ── xData helpers ─────────────────────────────────────────────────────────────
void EnsureAppRegistered(AcDbDatabase* pDb, const TCHAR* appName)
{
    AcDbRegAppTable* pTable = nullptr;
    if (pDb->getRegAppTable(pTable, AcDb::kForRead) != Acad::eOk) return;
    if (!pTable->has(appName))
    {
        pTable->upgradeOpen();
        AcDbRegAppTableRecord* pRec = new AcDbRegAppTableRecord();
        pRec->setName(appName);
        pTable->add(pRec);
        pRec->close();
    }
    pTable->close();
}

// Internal: write a single {appName, textHandle} xData pair onto a curve.
static void WriteCurveTextXData(AcDbObjectId curveId, AcDbObjectId textId,
                                 const TCHAR* appName)
{
    TCHAR handleBuf[64] = {};
    textId.handle().getIntoAsciiBuffer(handleBuf);

    CommonTools::AcDbObjectGuard<AcDbEntity> curve(curveId, AcDb::kForWrite);
    if (!curve) return;

    EnsureAppRegistered(curve->database(), appName);

    resbuf* pRb = acutBuildList(
        AcDb::kDxfRegAppName, appName,
        AcDb::kDxfXdHandle,   handleBuf,
        RTNONE);
    if (pRb) { curve->setXData(pRb); acutRelRb(pRb); }
}

void StoreAreaXData        (AcDbObjectId c, AcDbObjectId t) { WriteCurveTextXData(c, t, AREA_APP_NAME);  }
void StorePerimXData       (AcDbObjectId c, AcDbObjectId t) { WriteCurveTextXData(c, t, PERIM_APP_NAME); }
void StoreSumXData         (AcDbObjectId c, AcDbObjectId t) { WriteCurveTextXData(c, t, SUM_APP_NAME);   }
void StoreLinearLengthXData(AcDbObjectId c, AcDbObjectId t) { WriteCurveTextXData(c, t, LL_APP_NAME);    }

void StoreRoomXData(AcDbObjectId curveId, AcDbObjectId textId,
                    const CString& roomName)
{
    TCHAR handleBuf[64] = {};
    textId.handle().getIntoAsciiBuffer(handleBuf);

    CommonTools::AcDbObjectGuard<AcDbEntity> curve(curveId, AcDb::kForWrite);
    if (!curve) return;

    EnsureAppRegistered(curve->database(), ROOM_APP_NAME);

    resbuf* pRb = acutBuildList(
        AcDb::kDxfRegAppName,    ROOM_APP_NAME,
        AcDb::kDxfXdHandle,      handleBuf,
        AcDb::kDxfXdAsciiString, (LPCTSTR)roomName,
        RTNONE);
    if (pRb) { curve->setXData(pRb); acutRelRb(pRb); }
}

void CollectXDataPairs(AcDbDatabase* pDb, const TCHAR* appName,
                       std::vector<std::pair<AcDbObjectId, AcDbObjectId>>& pairs)
{
    AcDbBlockTable* pBT = nullptr;
    if (pDb->getBlockTable(pBT, AcDb::kForRead) != Acad::eOk) return;
    AcDbBlockTableRecord* pBTR = nullptr;
    if (pBT->getAt(ACDB_MODEL_SPACE, pBTR, AcDb::kForRead) != Acad::eOk)
    { pBT->close(); return; }
    pBT->close();

    AcDbBlockTableRecordIterator* pRawIter = nullptr;
    pBTR->newIterator(pRawIter);
    pBTR->close();
    if (!pRawIter) return;

    for (; !pRawIter->done(); pRawIter->step())
    {
        AcDbObjectId entId, textId;
        {
            AcDbObjectId id;
            pRawIter->getEntityId(id);

            CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
            if (!ent || !ent->isKindOf(AcDbCurve::desc())) continue;

            resbuf* pRb = ent->xData(appName);
            if (!pRb) continue;

            for (resbuf* p = pRb; p; p = p->rbnext)
            {
                if (p->restype == AcDb::kDxfXdHandle)
                {
                    AcDbHandle h(p->resval.rstring);
                    pDb->getAcDbObjectId(textId, Adesk::kFalse, h);
                    break;
                }
            }
            acutRelRb(pRb);
            entId = id;
        }
        if (!entId.isNull() && !textId.isNull())
            pairs.push_back({ entId, textId });
    }
    delete pRawIter;
}

} // namespace CadInfra
