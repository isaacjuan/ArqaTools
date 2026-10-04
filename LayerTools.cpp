#include "StdAfx.h"
#include "LayerTools.h"
#include "CommonTools.h"
#include "CadInfra.h"

namespace LayerTools
{
    // CHGTOLAYER command - Change selected objects to current layer
    void changeToCurrentLayerCommand()
    {
        acutPrintf(_T("\n=== CHANGE TO CURRENT LAYER ===\n"));
        
        // Get current layer
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb)
        {
            acutPrintf(_T("Error: No active database.\n"));
            return;
        }
        
        AcDbObjectId currentLayerId = pDb->clayer();
        
        // Open layer table record to get layer name
        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layerRec(currentLayerId);
        if (!layerRec) { acutPrintf(_T("Error: Cannot access current layer.\n")); return; }
        const ACHAR* layerName = nullptr;
        layerRec->getName(layerName);
        CString currentLayerName(layerName);
        
        acutPrintf(_T("Current layer: %s\n"), (LPCTSTR)currentLayerName);
        acutPrintf(_T("Select objects to change to this layer:\n"));
        
        std::vector<AcDbObjectId> ids = CommonTools::SelectIds();
        if (ids.empty())
        {
            acutPrintf(_T("No objects selected.\n"));
            return;
        }
        acutPrintf(_T("Processing %d objects...\n"), static_cast<int>(ids.size()));

        int successCount = 0;
        int failCount = 0;
        for (AcDbObjectId objId : ids)
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
            if (ent && ent->setLayer(currentLayerId) == Acad::eOk) successCount++;
            else failCount++;
        }

        acutPrintf(_T("\n✓ Changed %d objects to layer '%s'\n"), successCount, (LPCTSTR)currentLayerName);
        if (failCount > 0)
        {
            acutPrintf(_T("⚠ Failed to change %d objects\n"), failCount);
        }
    }
    
    // NL command - Quick new layer creation and set as current
    void newLayerCommand()
    {
        acutPrintf(_T("\n=== QUICK NEW LAYER ===\n"));
        
        // Get layer name from user
        TCHAR layerNameBuffer[256];
        int result = acedGetString(1, _T("Layer name: "), layerNameBuffer);
        
        if (result != RTNORM)
        {
            acutPrintf(_T("\nCommand cancelled.\n"));
            return;
        }
        
        CString layerName(layerNameBuffer);
        layerName.Trim();
        
        if (layerName.IsEmpty())
        {
            acutPrintf(_T("Error: Layer name cannot be empty.\n"));
            return;
        }
        
        bool created = false;
        CString err;
        if (!SetCurrentLayer(layerName, true, &created, &err))
        {
            acutPrintf(_T("Error: %s\n"), (LPCTSTR)err);
            return;
        }
        if (created)
            acutPrintf(_T("✓ Layer '%s' created and set as current.\n"), (LPCTSTR)layerName);
        else
            acutPrintf(_T("✓ Layer '%s' already exists, set as current.\n"), (LPCTSTR)layerName);
    }

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
// MATCHLAYER command - Change selected objects to the layer of a source object
void LayerTools::freezeLayerCommand()
{
    acutPrintf(_T("\n=== FREEZE LAYER ===\n"));

    ads_name ent;
    ads_point pt;
    if (acedEntSel(_T("\nSelect object on layer to freeze: "), ent, pt) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.\n")); return; }

    AcDbObjectId objId;
    acdbGetObjectId(objId, ent);

    AcDbObjectId layerId;
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId);
        if (!ent) { acutPrintf(_T("\nError: Cannot open object.\n")); return; }
        layerId = ent->layerId();
    }

    CString layerName;
    {
        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(layerId);
        if (!layer) { acutPrintf(_T("\nError: Cannot open layer record.\n")); return; }
        const ACHAR* lName = nullptr;
        layer->getName(lName);
        layerName = lName;
    }

    CString err;
    if (!SetLayerState(layerName, 1, -1, -1, &err))
    { acutPrintf(_T("\n%s.\n"), (LPCTSTR)err); return; }

    acutPrintf(_T("\nLayer '%s' frozen.\n"), (LPCTSTR)layerName);
}

void LayerTools::matchLayerCommand()
{
    acutPrintf(_T("\n=== MATCH LAYER ===\n"));

    // Pick source object
    ads_name srcEnt;
    ads_point srcPt;
    if (acedEntSel(_T("\nSelect SOURCE object (to match layer from): "), srcEnt, srcPt) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.\n")); return; }

    AcDbObjectId srcId;
    acdbGetObjectId(srcId, srcEnt);

    AcDbObjectId layerId;
    CString layerName;
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> src(srcId);
        if (!src) { acutPrintf(_T("\nError: Cannot open source object.\n")); return; }
        layerId = src->layerId();
    }
    {
        CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> lr(layerId);
        if (lr) { const ACHAR* n = nullptr; lr->getName(n); layerName = n; }
    }

    acutPrintf(_T("Source layer: %s\n"), (LPCTSTR)layerName);
    acutPrintf(_T("Select objects to move to this layer:\n"));

    std::vector<AcDbObjectId> ids = CommonTools::SelectIds();
    if (ids.empty()) { acutPrintf(_T("\nNo objects selected.\n")); return; }

    int count = 0;
    for (AcDbObjectId objId : ids)
    {
        if (objId == srcId) continue; // skip source itself
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
        if (ent) { ent->setLayer(layerName); count++; }
    }

    acutPrintf(_T("\nLayer matched to '%s' on %d object(s).\n"), (LPCTSTR)layerName, count);
}