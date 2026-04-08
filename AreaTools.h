#pragma once
#include "StdAfx.h"
#include <vector>

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
void roomTagCommand();
void perimeterCommand();
void linearLengthCommand();
void countBlocksCommand();
void splitLineCommand();
void splitPoliCommand();
void tagAllCommand();

// Persistence lifecycle — call from ARX init / unload
void InitAreaToolsPersistence();
void UninitAreaToolsPersistence();
