// TextTools.cpp - Text Manipulation Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "TextTools.h"
#include "CommonTools.h"
#include "dbmtext.h"
#include "dbdim.h"

namespace TextTools
{
    // ============================================================================
    // ENUMERATIONS
    // ============================================================================
    
    // Text type enumeration
    enum TextType
    {
        TextType_None = 0,
        TextType_DbText = 1,
        TextType_MText = 2
    };

    // ============================================================================
    // TYPE IDENTIFICATION HELPERS
    // ============================================================================
    
    // Determine text type and cast to appropriate pointer
    // Returns TextType enum and optionally fills in typed pointers
    static TextType GetTextType(AcDbObject* pObj, AcDbText** ppText, AcDbMText** ppMText)
    {
        if (!pObj)
            return TextType_None;

        AcDbText* pText = AcDbText::cast(pObj);
        if (pText)
        {
            if (ppText) *ppText = pText;
            return TextType_DbText;
        }

        AcDbMText* pMText = AcDbMText::cast(pObj);
        if (pMText)
        {
            if (ppMText) *ppMText = pMText;
            return TextType_MText;
        }

        return TextType_None;
    }

    // Check if entity is a dimension
    static bool IsDimensionEntity(AcDbObject* pObj)
    {
        if (!pObj)
            return false;
        return AcDbDimension::cast(pObj) != nullptr;
    }

    // ============================================================================
    // TEXT CONTENT MANIPULATION HELPERS
    // ============================================================================
    
    // Set text content to a text entity
    // Supports both AcDbText and AcDbMText
    static bool SetTextContent(AcDbEntity* pEnt, const TCHAR* content)
    {
        if (!pEnt || !content)
            return false;

        AcDbText* pText = nullptr;
        AcDbMText* pMText = nullptr;
        TextType type = GetTextType(pEnt, &pText, &pMText);

        // Set text content based on type
        switch (type)
        {
        case TextType_DbText:
            if (pText)
            {
                pText->setTextString(content);
                return true;
            }
            break;

        case TextType_MText:
            if (pMText)
            {
                pMText->setContents(content);
                return true;
            }
            break;
        }

        return false;
    }
}

// ============================================================================
// Non-interactive cores (used by the Lua bindings; the commands are Lua files)
// ============================================================================
namespace TextTools
{
    bool GetText(AcDbObjectId id, CString& out)
    {
        CommonTools::AcDbObjectGuard<AcDbObject> obj(id);
        AcDbText* pText = nullptr; AcDbMText* pMText = nullptr;
        switch (GetTextType(obj.get(), &pText, &pMText))
        {
        case TextType_DbText: out = pText->textStringConst(); return true;
        case TextType_MText:  { AcString c; pMText->contents(c); out = c.kwszPtr(); return true; }
        default:              return false;
        }
    }

    bool SetText(AcDbObjectId id, const CString& text)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
        return ent && SetTextContent(ent.get(), text);
    }

    int CopyTextStyle(AcDbObjectId srcId, const std::vector<AcDbObjectId>& destIds,
                      bool includeHeight, int* skippedOut)
    {
        AcDbObjectId styleId;
        double height = 0.0, widthFactor = 1.0, oblique = 0.0;
        AcDb::TextHorzMode horzMode = AcDb::kTextLeft;
        AcDb::TextVertMode vertMode = AcDb::kTextBase;
        {
            CommonTools::AcDbObjectGuard<AcDbObject> pSrc(srcId);
            AcDbText* pST = nullptr; AcDbMText* pSMT = nullptr;
            TextType srcType = GetTextType(pSrc.get(), &pST, &pSMT);
            if (srcType == TextType_DbText)
            {
                styleId = pST->textStyle(); height = pST->height();
                widthFactor = pST->widthFactor(); oblique = pST->oblique();
                horzMode = pST->horizontalMode(); vertMode = pST->verticalMode();
            }
            else if (srcType == TextType_MText)
            { styleId = pSMT->textStyle(); height = pSMT->textHeight(); }
            else
                return -1;
        }

        int updated = 0, skipped = 0;
        for (AcDbObjectId destId : destIds)
        {
            if (destId == srcId) { skipped++; continue; }

            CommonTools::AcDbObjectGuard<AcDbObject> pDest(destId, AcDb::kForWrite);
            if (!pDest || IsDimensionEntity(pDest.get())) { skipped++; continue; }

            AcDbText* pDT = nullptr; AcDbMText* pDMT = nullptr;
            TextType dt = GetTextType(pDest.get(), &pDT, &pDMT);
            if (dt == TextType_DbText)
            {
                pDT->setTextStyle(styleId);
                if (includeHeight) pDT->setHeight(height);
                pDT->setWidthFactor(widthFactor);
                pDT->setOblique(oblique);
                pDT->setHorizontalMode(horzMode);
                pDT->setVerticalMode(vertMode);
                updated++;
            }
            else if (dt == TextType_MText)
            {
                pDMT->setTextStyle(styleId);
                if (includeHeight) pDMT->setTextHeight(height);
                updated++;
            }
            else
                skipped++;
        }
        if (skippedOut) *skippedOut = skipped;
        return updated;
    }

    int CopyDimStyle(AcDbObjectId srcId, const std::vector<AcDbObjectId>& destIds, int* skippedOut)
    {
        AcDbObjectId dimStyleId;
        {
            CommonTools::AcDbObjectGuard<AcDbObject> pSrc(srcId);
            AcDbDimension* pSrcDim = pSrc ? AcDbDimension::cast(pSrc.get()) : nullptr;
            if (!pSrcDim) return -1;
            dimStyleId = pSrcDim->dimensionStyle();
        }

        int updated = 0, skipped = 0;
        for (AcDbObjectId destId : destIds)
        {
            if (destId == srcId) { skipped++; continue; }
            CommonTools::AcDbObjectGuard<AcDbObject> pDest(destId, AcDb::kForWrite);
            AcDbDimension* pDim = pDest ? AcDbDimension::cast(pDest.get()) : nullptr;
            if (pDim) { pDim->setDimensionStyle(dimStyleId); updated++; }
            else      { skipped++; }
        }
        if (skippedOut) *skippedOut = skipped;
        return updated;
    }

    bool ParseNumber(CString text, double& value)
    {
        text.Trim();
        text.Replace(_T("$"), _T(""));
        text.Replace(_T("€"), _T(""));
        text.Replace(_T("£"), _T(""));
        text.Replace(_T(" "), _T(""));
        text.Replace(_T(","), _T(""));
        if (text.IsEmpty()) return false;

        TCHAR* endPtr = nullptr;
        value = _tcstod(text, &endPtr);
        return endPtr != nullptr && (*endPtr == _T('\0') || *endPtr == _T('\n'));
    }

    double SumTextValues(const std::vector<AcDbObjectId>& ids, int* validOut, int* invalidOut,
                         bool verbose)
    {
        double total = 0.0;
        int valid = 0, invalid = 0;
        for (AcDbObjectId id : ids)
        {
            CString content;
            if (!GetText(id, content)) continue;   // not text: ignored, not counted

            double v = 0.0;
            if (ParseNumber(content, v))
            {
                total += v; valid++;
                if (verbose) acutPrintf(_T("  %s = %.2f\n"), (LPCTSTR)content, v);
            }
            else
            {
                invalid++;
                if (verbose) acutPrintf(_T("  Skipped '%s' (not numeric)\n"), (LPCTSTR)content);
            }
        }
        if (validOut)   *validOut = valid;
        if (invalidOut) *invalidOut = invalid;
        return total;
    }

    int ScaleTextHeight(const std::vector<AcDbObjectId>& ids, double factor)
    {
        int count = 0;
        for (AcDbObjectId id : ids)
        {
            CommonTools::AcDbObjectGuard<AcDbEntity> ent(id, AcDb::kForWrite);
            if (!ent) continue;
            if (auto* t = AcDbText::cast(ent.get()))
            { t->setHeight(t->height() * factor); count++; }
            else if (auto* m = AcDbMText::cast(ent.get()))
            { m->setTextHeight(m->textHeight() * factor); count++; }
            // AcDbDimension text height is controlled by the dim style.
        }
        return count;
    }
}
