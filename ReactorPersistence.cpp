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
static std::vector<CurveTextReactor*>         g_labels;   // one curve, one label
static std::vector<PolylineSumLengthReactor*> g_sum;      // many curves, one label

// ── Registration ──────────────────────────────────────────────────────────────
void Register(CurveTextReactor*         r) { g_labels.push_back(r); }
void Register(PolylineSumLengthReactor* r) { g_sum.push_back(r); }

// ── Rebuild from saved xdata ─────────────────────────────────────────────────

// One row per single-curve label kind: its xdata app, the entity type its
// label must be, and how to recreate its reactor.
struct LabelKind
{
    const TCHAR*      appName;
    AcRxClass*        labelClass;
    CurveTextReactor* (*make)(const CadInfra::XDataLink&);
};

static void RebuildLabels(AcDbDatabase* pDb)
{
    const LabelKind kinds[] = {
        { CadInfra::AREA_APP_NAME,  AcDbText::desc(),
          [](const CadInfra::XDataLink& l) -> CurveTextReactor* { return new PolylineAreaReactor(l.curveId, l.labelId); } },
        { CadInfra::PERIM_APP_NAME, AcDbText::desc(),
          [](const CadInfra::XDataLink& l) -> CurveTextReactor* { return new PerimeterReactor(l.curveId, l.labelId); } },
        { CadInfra::LL_APP_NAME,    AcDbText::desc(),
          [](const CadInfra::XDataLink& l) -> CurveTextReactor* { return new LinearLengthReactor(l.curveId, l.labelId); } },
        { CadInfra::ROOM_APP_NAME,  AcDbMText::desc(),
          [](const CadInfra::XDataLink& l) -> CurveTextReactor* { return new RoomTagReactor(l.curveId, l.labelId, l.text); } },
    };

    // Keyed by label, so one curve can carry several label kinds.
    std::set<AcDbObjectId> already;
    for (auto* r : g_labels) already.insert(r->getLabelId());

    for (const LabelKind& kind : kinds)
    {
        for (const CadInfra::XDataLink& link : CadInfra::CollectXDataLinks(pDb, kind.appName))
        {
            if (already.count(link.labelId)) continue;
            {
                CommonTools::AcDbObjectGuard<AcDbEntity> t(link.labelId);
                if (!t || !t->isKindOf(kind.labelClass)) continue;
            }
            g_labels.push_back(kind.make(link));
            already.insert(link.labelId);
        }
    }
}

static void RebuildSum(AcDbDatabase* pDb)
{
    std::set<AcDbObjectId> already;
    for (auto* r : g_sum) already.insert(r->getTextId());

    // Group curve IDs by textId — each unique textId gets one reactor.
    std::map<AcDbObjectId, std::vector<AcDbObjectId>> byText;
    for (const CadInfra::XDataLink& link : CadInfra::CollectXDataLinks(pDb, CadInfra::SUM_APP_NAME))
    {
        if (!already.count(link.labelId))
            byText[link.labelId].push_back(link.curveId);
    }

    for (auto& [textId, curveIds] : byText)
    {
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> t(textId);
            if (!t || !t->isKindOf(AcDbText::desc())) continue;
        }
        g_sum.push_back(new PolylineSumLengthReactor(curveIds, textId));
    }
}

static void RebuildAll(AcDbDatabase* pDb)
{
    RebuildLabels(pDb);
    RebuildSum(pDb);
}

// Deletes (and drops) the reactors whose key object belongs to pDb, or all of
// them when pDb is null.
template<typename R, typename Key>
static void DeleteReactors(std::vector<R*>& vec, AcDbDatabase* pDb, Key key)
{
    auto it = vec.begin();
    while (it != vec.end())
    {
        if (!pDb || key(*it).database() == pDb) { delete *it; it = vec.erase(it); }
        else ++it;
    }
}

static void DeleteAll(AcDbDatabase* pDb)
{
    DeleteReactors(g_labels, pDb, [](CurveTextReactor* r) { return r->getCurveId(); });
    DeleteReactors(g_sum,    pDb, [](PolylineSumLengthReactor* r) { return r->getTextId(); });
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
        if (pDoc && pDoc->database())
            DeleteAll(pDoc->database());
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
    DeleteAll(nullptr);
}

} // namespace ReactorPersistence
