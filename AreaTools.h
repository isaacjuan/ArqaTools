#pragma once
#include "StdAfx.h"
#include <vector>
#include <map>

// ============================================================================
// CurveTextReactor - base class (Template Method pattern)
//
// Manages the attach/detach lifecycle and the erased/modified dispatch for
// any reactor that monitors one curve and owns one label entity.
// Derived classes implement only updateLabel() — the variant step.
// ============================================================================
class CurveTextReactor : public AcDbObjectReactor
{
public:
    CurveTextReactor(AcDbObjectId curveId, AcDbObjectId labelId);
    virtual ~CurveTextReactor();

    void modified(const AcDbObject* pObj)                        override;
    void erased  (const AcDbObject* pObj, Adesk::Boolean) override;

    AcDbObjectId getCurveId() const { return m_curveId; }

protected:
    AcDbObjectId m_curveId;
    AcDbObjectId m_labelId;

    virtual void updateLabel() = 0;
};

// ── Concrete reactors ────────────────────────────────────────────────────────

class PolylineAreaReactor : public CurveTextReactor
{
public:
    PolylineAreaReactor(AcDbObjectId polylineId, AcDbObjectId textId)
        : CurveTextReactor(polylineId, textId) {}
private:
    void updateLabel() override;
};

class PerimeterReactor : public CurveTextReactor
{
public:
    PerimeterReactor(AcDbObjectId polylineId, AcDbObjectId textId)
        : CurveTextReactor(polylineId, textId) {}
private:
    void updateLabel() override;
};

class LinearLengthReactor : public CurveTextReactor
{
public:
    LinearLengthReactor(AcDbObjectId curveId, AcDbObjectId textId)
        : CurveTextReactor(curveId, textId) {}
private:
    void updateLabel() override;
};

class RoomTagReactor : public CurveTextReactor
{
public:
    RoomTagReactor(AcDbObjectId polylineId, AcDbObjectId mtextId, const CString& roomName)
        : CurveTextReactor(polylineId, mtextId), m_roomName(roomName) {}
private:
    CString m_roomName;
    void updateLabel() override;
};

// ── Independent reactor (multi-curve, different base class) ─────────────────

class PolylineSumLengthReactor : public AcDbEntityReactor
{
public:
    PolylineSumLengthReactor(const std::vector<AcDbObjectId>& polylineIds, AcDbObjectId textId);
    virtual ~PolylineSumLengthReactor();

    void modified    (const AcDbObject* pObj)                        override;
    void erased      (const AcDbObject* pObj, Adesk::Boolean) override;
    void highlighted (const AcDbEntity* pEnt, Adesk::Boolean bHighlight);

    AcDbObjectId getTextId() const { return m_textId; }

private:
    std::vector<AcDbObjectId> m_polylineIds;
    AcDbObjectId              m_textId;

    void updateSumLengthText();
    void removePolylineFromList(AcDbObjectId polylineId);
    void highlightLinkedCurves(bool bHighlight);
};

// Formatting and CAD infrastructure moved to MeasureFormat.h and CadInfra.h

// Area / measurement commands
void insertAreaCommand();
void sumLengthCommand();

// New architectural commands
void perimeterCommand();
void linearLengthCommand();
void countBlocksCommand();
void splitLineCommand();
void splitPoliCommand();
void tagAllCommand();

// ── Non-interactive cores (used by the commands above and the Lua bindings).
// Every label is reactor-linked: it updates when its curve changes and is
// erased with it, and the link survives save/reopen (xdata + ReactorPersistence).
// On failure they return kNull / an empty list and, when err is given, a reason.
namespace AreaTools
{
    AcDbObjectId InsertAreaLabel(AcDbObjectId polylineId, CString* err = nullptr);
    AcDbObjectId InsertPerimeterLabel(AcDbObjectId polylineId, CString* err = nullptr);
    AcDbObjectId InsertRoomTag(AcDbObjectId polylineId, const CString& roomName,
                               CString* err = nullptr);
    // Perpendicular length label at the curve's midpoint.
    AcDbObjectId InsertLengthLabel(AcDbObjectId curveId, const CString& layerName = CString());
    // One label at pos showing the summed length of curveIds (non-curves skipped).
    AcDbObjectId InsertSumLengthLabel(const std::vector<AcDbObjectId>& curveIds,
                                      const AcGePoint3d& pos, double* total = nullptr,
                                      int* monitored = nullptr, CString* err = nullptr);
    // Block name -> instance count, among ids (nullptr = all of model space).
    // Anonymous blocks (*U..., *D...) are skipped.
    std::map<CString, int> CountBlocks(const std::vector<AcDbObjectId>* ids);
    // Replace the base curve by segments split at its intersections with
    // crossIds (the base is erased). SplitLine produces lines, SplitPolyline
    // keeps arc segments. Optionally tags each segment with a length label.
    std::vector<AcDbObjectId> SplitLine(AcDbObjectId baseId, const std::vector<AcDbObjectId>& crossIds,
                                        const CString& targetLayer, bool tagLengths,
                                        CString* err = nullptr);
    std::vector<AcDbObjectId> SplitPolyline(AcDbObjectId baseId, const std::vector<AcDbObjectId>& crossIds,
                                            const CString& targetLayer, bool tagLengths,
                                            CString* err = nullptr);
}

// Persistence lifecycle — call from ARX init / unload
void InitAreaToolsPersistence();
void UninitAreaToolsPersistence();
