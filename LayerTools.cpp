#include "StdAfx.h"
#include "LayerTools.h"
#include "CommonTools.h"

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
        
        // Select objects
        ads_name ss;
        if (acedSSGet(NULL, NULL, NULL, NULL, ss) != RTNORM)
        {
            acutPrintf(_T("No objects selected.\n"));
            return;
        }
        
        // Get selection set length
        Adesk::Int32 length = 0;
        acedSSLength(ss, &length);
        
        if (length == 0)
        {
            acedSSFree(ss);
            acutPrintf(_T("Selection set is empty.\n"));
            return;
        }
        
        acutPrintf(_T("Processing %d objects...\n"), length);
        
        int successCount = 0;
        int failCount = 0;
        
        // Process each object
        CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId objId)
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
            if (ent) { if (ent->setLayer(currentLayerId) == Acad::eOk) successCount++; else failCount++; }
            else failCount++;
        });
        
        acedSSFree(ss);
        
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
        
        // Get database
        AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
        if (!pDb)
        {
            acutPrintf(_T("Error: No active database.\n"));
            return;
        }
        
        // Open layer table
        AcDbLayerTable* pLayerTable = nullptr;
        if (pDb->getLayerTable(pLayerTable, AcDb::kForWrite) != Acad::eOk)
        {
            acutPrintf(_T("Error: Cannot access layer table.\n"));
            return;
        }
        
        // Check if layer already exists
        if (pLayerTable->has(layerName))
        {
            acutPrintf(_T("Layer '%s' already exists. Setting as current...\n"), (LPCTSTR)layerName);
            
            // Get existing layer ID
            AcDbObjectId layerId;
            if (pLayerTable->getAt(layerName, layerId) == Acad::eOk)
            {
                pDb->setClayer(layerId);
                acutPrintf(_T("✓ Layer '%s' set as current.\n"), (LPCTSTR)layerName);
            }
            else
            {
                acutPrintf(_T("Error: Cannot get layer ID.\n"));
            }
            
            pLayerTable->close();
            return;
        }
        
        // Create new layer
        AcDbLayerTableRecord* pNewLayer = new AcDbLayerTableRecord();
        pNewLayer->setName(layerName);
        
        // Set default color (white/7)
        AcCmColor color;
        color.setColorIndex(7);
        pNewLayer->setColor(color);
        
        // Add layer to layer table
        AcDbObjectId newLayerId;
        if (pLayerTable->add(newLayerId, pNewLayer) == Acad::eOk)
        {
            pNewLayer->close();
            pLayerTable->close();
            
            // Set new layer as current
            if (pDb->setClayer(newLayerId) == Acad::eOk)
            {
                acutPrintf(_T("✓ Layer '%s' created and set as current.\n"), (LPCTSTR)layerName);
            }
            else
            {
                acutPrintf(_T("✓ Layer '%s' created but could not set as current.\n"), (LPCTSTR)layerName);
            }
        }
        else
        {
            pNewLayer->close();
            pLayerTable->close();
            acutPrintf(_T("Error: Could not create layer.\n"));
        }
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

    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    if (layerId == pDb->clayer())
    { acutPrintf(_T("\nCannot freeze the current layer.\n")); return; }

    CommonTools::AcDbObjectGuard<AcDbLayerTableRecord> layer(layerId, AcDb::kForWrite);
    if (!layer) { acutPrintf(_T("\nError: Cannot open layer record.\n")); return; }

    const ACHAR* lName = nullptr;
    layer->getName(lName);
    CString layerName(lName);

    if (layerName.CompareNoCase(_T("0")) == 0)
    { acutPrintf(_T("\nCannot freeze layer 0.\n")); return; }

    layer->setIsFrozen(Adesk::kTrue);

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

    ads_name ss;
    if (acedSSGet(NULL, NULL, NULL, NULL, ss) != RTNORM)
    { acutPrintf(_T("\nNo objects selected.\n")); return; }

    Adesk::Int32 len = 0;
    acedSSLength(ss, &len);
    int count = 0;

    CommonTools::ForEachSsEntity(ss, len, [&](AcDbObjectId objId)
    {
        if (objId == srcId) return; // skip source itself
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
        if (ent) { ent->setLayer(layerName); count++; }
    });

    acedSSFree(ss);
    acutPrintf(_T("\nLayer matched to '%s' on %d object(s).\n"), (LPCTSTR)layerName, count);
}