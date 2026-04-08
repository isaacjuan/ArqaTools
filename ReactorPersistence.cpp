#include "StdAfx.h"
#include "ReactorPersistence.h"
#include "CadInfra.h"
#include "CommonTools.h"
#include "acdocman.h"
#include "dbapserv.h"
#include "dbents.h"
#include <vector>
#include <set>
#include <map>

namespace ReactorPersistence
{

// ── In-memory reactor stores ──────────────────────────────────────────────────
static std::vector<PolylineAreaReactor*>      g_area;
static std::vector<PerimeterReactor*>         g_perim;
static std::vector<RoomTagReactor*>           g_room;
static std::vector<PolylineSumLengthReactor*> g_sum;
static std::vector<LinearLengthReactor*>      g_ll;

// ── Registration ──────────────────────────────────────────────────────────────
void Register(PolylineAreaReactor*      r) { g_area.push_back(r); }
void Register(PerimeterReactor*         r) { g_perim.push_back(r); }
void Register(RoomTagReactor*           r) { g_room.push_back(r); }
void Register(PolylineSumLengthReactor* r) { g_sum.push_back(r); }
void Register(LinearLengthReactor*      r) { g_ll.push_back(r); }

// ── Rebuild helpers ───────────────────────────────────────────────────────────

static void RebuildArea(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_area) already.insert(r->getCurveId());

    std::vector<std::pair<AcDbObjectId, AcDbObjectId>> pairs;
    CadInfra::CollectXDataPairs(pDb, CadInfra::AREA_APP_NAME, pairs);

    for (auto& [curveId, textId] : pairs)
    {
        if (already.count(curveId)) continue;
        CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
        if (!t || !t->isKindOf(AcDbText::desc())) continue;
        g_area.push_back(new PolylineAreaReactor(curveId, textId));
    }
}

static void RebuildPerim(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_perim) already.insert(r->getCurveId());

    std::vector<std::pair<AcDbObjectId, AcDbObjectId>> pairs;
    CadInfra::CollectXDataPairs(pDb, CadInfra::PERIM_APP_NAME, pairs);

    for (auto& [curveId, textId] : pairs)
    {
        if (already.count(curveId)) continue;
        CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
        if (!t || !t->isKindOf(AcDbText::desc())) continue;
        g_perim.push_back(new PerimeterReactor(curveId, textId));
    }
}

static void RebuildRoom(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_room) already.insert(r->getCurveId());

    // Room xData also carries a room-name string — must scan manually.
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
        CString roomName;
        {
            AcDbObjectId id;
            pRawIter->getEntityId(id);
            if (already.count(id)) continue;

            CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
            if (!ent || !ent->isKindOf(AcDbCurve::desc())) continue;

            resbuf* pRb = ent->xData(CadInfra::ROOM_APP_NAME);
            if (!pRb) continue;

            for (resbuf* p = pRb; p; p = p->rbnext)
            {
                if (p->restype == AcDb::kDxfXdHandle)
                {
                    AcDbHandle h(p->resval.rstring);
                    pDb->getAcDbObjectId(textId, Adesk::kFalse, h);
                }
                else if (p->restype == AcDb::kDxfXdAsciiString)
                    roomName = p->resval.rstring;
            }
            acutRelRb(pRb);
            entId = id;
        }
        if (entId.isNull() || textId.isNull()) continue;
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
            if (!t || !t->isKindOf(AcDbMText::desc())) continue;
        }
        g_room.push_back(new RoomTagReactor(entId, textId, roomName));
    }
    delete pRawIter;
}

static void RebuildSum(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_sum) already.insert(r->getTextId());

    std::vector<std::pair<AcDbObjectId, AcDbObjectId>> pairs;
    CadInfra::CollectXDataPairs(pDb, CadInfra::SUM_APP_NAME, pairs);

    // Group curve IDs by textId — each unique textId gets one reactor.
    std::map<AcDbObjectId, std::vector<AcDbObjectId>> byText;
    for (auto& [curveId, textId] : pairs)
    {
        if (!already.count(textId))
            byText[textId].push_back(curveId);
    }

    for (auto& [textId, curveIds] : byText)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
        if (!t || !t->isKindOf(AcDbText::desc())) continue;
        g_sum.push_back(new PolylineSumLengthReactor(curveIds, textId));
    }
}

static void RebuildLL(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_ll) already.insert(r->getCurveId());

    std::vector<std::pair<AcDbObjectId, AcDbObjectId>> pairs;
    CadInfra::CollectXDataPairs(pDb, CadInfra::LL_APP_NAME, pairs);

    for (auto& [curveId, textId] : pairs)
    {
        if (already.count(curveId)) continue;
        CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
        if (!t || !t->isKindOf(AcDbText::desc())) continue;
        g_ll.push_back(new LinearLengthReactor(curveId, textId));
    }
}

static void RebuildAll(AcDbDatabase* pDb)
{
    RebuildArea(pDb);
    RebuildPerim(pDb);
    RebuildRoom(pDb);
    RebuildSum(pDb);
    RebuildLL(pDb);
}

// ── Document lifecycle reactor ────────────────────────────────────────────────
class DocReactor : public AcApDocManagerReactor
{
public:
    void documentActivated(AcApDocument* pDoc) override
    {
        if (pDoc && pDoc->database())
            RebuildAll(pDoc->database());
    }

    void documentToBeDestroyed(AcApDocument* pDoc) override
    {
        if (!pDoc || !pDoc->database()) return;
        AcDbDatabase* pDb = pDoc->database();

        auto eraseByDb = [&](auto& vec)
        {
            auto it = vec.begin();
            while (it != vec.end())
            {
                if ((*it)->getCurveId().database() == pDb)
                { delete *it; it = vec.erase(it); }
                else ++it;
            }
        };
        eraseByDb(g_area);
        eraseByDb(g_perim);
        eraseByDb(g_room);
        eraseByDb(g_ll);

        // PolylineSumLengthReactor has no getCurveId() — use textId database.
        auto it = g_sum.begin();
        while (it != g_sum.end())
        {
            if ((*it)->getTextId().database() == pDb)
            { delete *it; it = g_sum.erase(it); }
            else ++it;
        }
    }
};

static DocReactor* g_docReactor = nullptr;

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void Init()
{
    g_docReactor = new DocReactor();
    acDocManager->addReactor(g_docReactor);

    // documentActivated won't fire retroactively for already-open documents
    // (e.g. after ARX reload), so rebuild them immediately.
    AcApDocumentIterator* pIter = acDocManager->newAcApDocumentIterator();
    for (; !pIter->done(); pIter->step())
    {
        AcApDocument* pDoc = pIter->document();
        if (pDoc && pDoc->database())
            RebuildAll(pDoc->database());
    }
    delete pIter;
}

void Uninit()
{
    if (g_docReactor)
    {
        acDocManager->removeReactor(g_docReactor);
        delete g_docReactor;
        g_docReactor = nullptr;
    }
    for (auto* r : g_area)  delete r;
    for (auto* r : g_perim) delete r;
    for (auto* r : g_room)  delete r;
    for (auto* r : g_sum)   delete r;
    for (auto* r : g_ll)    delete r;
    g_area.clear();
    g_perim.clear();
    g_room.clear();
    g_sum.clear();
    g_ll.clear();
}

} // namespace ReactorPersistence
