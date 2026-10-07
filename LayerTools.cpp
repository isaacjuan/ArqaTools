#include "StdAfx.h"
#include "LayerTools.h"
#include "CommonTools.h"
#include "CadInfra.h"

namespace LayerTools
{
    CString GetCurrentLayer()
    {
        CString name;
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb) return name;
        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> rec(pDb->clayer());
        if (rec) { const ACHAR* n = nullptr; rec->getName(n); name = n; }
        return name;
    }

    bool SetCurrentLayer(const CString& name, bool create, bool* created, CString* err)
    {
        auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return false; };
        if (created) *created = false;

        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb) return fail(_T("no active database"));

        if (create)
        {
            // New layer, default color white/7 (same as ATNL always did).
            CadInfra::LayerProps props;
            props.colorIndex = 7;
            bool made = CadInfra::EnsureLayer(name, props);
            if (created) *created = made;
        }

        AcDbObjectId layerId;
        {
            AcDbLayerTable* pLayerTable = nullptr;
            if (pDb->getLayerTable(pLayerTable, AcDb::kForRead) != Acad::eOk)
                return fail(_T("cannot access layer table"));
            Acad::ErrorStatus es = pLayerTable->getAt(name, layerId);
            pLayerTable->close();
            if (es != Acad::eOk)
                return fail(create ? _T("could not create layer") : _T("layer not found"));
        }

        if (pDb->setClayer(layerId) != Acad::eOk)
            return fail(_T("could not set layer as current (is it frozen?)"));
        return true;
    }

    bool SetLayerState(const CString& name, int frozen, int off, int locked, CString* err)
    {
        auto fail = [err](const TCHAR* msg) { if (err) *err = msg; return false; };

        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb) return fail(_T("no active database"));

        AcDbObjectId layerId;
        {
            AcDbLayerTable* pLayerTable = nullptr;
            if (pDb->getLayerTable(pLayerTable, AcDb::kForRead) != Acad::eOk)
                return fail(_T("cannot access layer table"));
            Acad::ErrorStatus es = pLayerTable->getAt(name, layerId);
            pLayerTable->close();
            if (es != Acad::eOk) return fail(_T("layer not found"));
        }

        if (frozen == 1)
        {
            if (layerId == pDb->clayer())         return fail(_T("cannot freeze the current layer"));
            if (name.CompareNoCase(_T("0")) == 0) return fail(_T("cannot freeze layer 0"));
        }

        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(layerId, AcDb::kForWrite);
        if (!layer) return fail(_T("cannot open layer record"));
        if (frozen >= 0) layer->setIsFrozen(frozen ? Adesk::kTrue : Adesk::kFalse);
        if (off    >= 0) layer->setIsOff   (off    ? Adesk::kTrue : Adesk::kFalse);
        if (locked >= 0) layer->setIsLocked(locked ? Adesk::kTrue : Adesk::kFalse);
        return true;
    }
}
