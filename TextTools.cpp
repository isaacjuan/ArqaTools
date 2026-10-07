// TextTools.cpp - Text Manipulation Tools Implementation

#include "StdAfx.h"
#include "ArqaTools.h"
#include "TextTools.h"
#include "CommonTools.h"
#include "CadInfra.h"
#include "dbmtext.h"
#include "dbdim.h"

namespace TextTools
{
    // ============================================================================
    // COMMAND HELPER CLASS - Static Members
    // ============================================================================
    
    const TCHAR* const CommandHelper::MSG_CANCELLED = _T("\nCommand cancelled.\n");
    const TCHAR* const CommandHelper::MSG_SOURCE_ID_ERROR = _T("\nError: Failed to get source entity ID.\n");
    const TCHAR* const CommandHelper::MSG_SOURCE_OPEN_ERROR = _T("\nError: Failed to open source entity.\n");
    const TCHAR* const CommandHelper::MSG_NO_DESTINATIONS = _T("\nNo destination objects selected.\n");

    // ============================================================================
    // COMMAND HELPER CLASS - Implementation
    // ============================================================================

    CommandHelper::CommandHelper(const TCHAR* commandName)
        : m_commandName(commandName)
        , m_hasSelection(false)
    {
        // Initialize selection set name
        m_destSelection[0] = 0;
        m_destSelection[1] = 0;
    }

    CommandHelper::~CommandHelper()
    {
        // Automatic cleanup of selection set
        if (m_hasSelection)
        {
            acedSSFree(m_destSelection);
            m_hasSelection = false;
        }
    }

    bool CommandHelper::SelectSource(ads_name& sourceEnt)
    {
        ads_point pickPt;
        return acedEntSel(_T("\nSelect source: "), sourceEnt, pickPt) == RTNORM;
    }

    bool CommandHelper::SelectDestinations()
    {
        if (acedSSGet(nullptr, nullptr, nullptr, nullptr, m_destSelection) == RTNORM)
        {
            m_hasSelection = true;
            return true;
        }
        return false;
    }

    void CommandHelper::PrintCommandCancelled() const
    {
        acutPrintf(MSG_CANCELLED);
    }

    void CommandHelper::PrintSourceIdError() const
    {
        acutPrintf(MSG_SOURCE_ID_ERROR);
    }

    void CommandHelper::PrintSourceOpenError() const
    {
        acutPrintf(MSG_SOURCE_OPEN_ERROR);
    }

    void CommandHelper::PrintNoDestinations() const
    {
        acutPrintf(MSG_NO_DESTINATIONS);
    }

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
    // ADS/OBJECTID CONVERSION HELPERS
    // ============================================================================
    
    // Convert ads_name to AcDbObjectId
    static bool GetObjectId(AcDbObjectId& objId, const ads_name& ename)
    {
        return acdbGetObjectId(objId, ename) == Acad::eOk;
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

    // ============================================================================
    // ENTITY SELECTION HELPERS
    // ============================================================================
    
    // ============================================================================
    // COMMANDS
    // ============================================================================

    // COPYTEXT command - Copy text content from one text object to others
    void copyTextCommand()
    {
        CommandHelper helper(_T("COPYTEXT"));
        acutPrintf(_T("\n=== COPY TEXT CONTENT ===\n"));
        
        // Select source text entity
        ads_name sourceEnt;
        if (!helper.SelectSource(sourceEnt))
        {
            helper.PrintCommandCancelled();
            return;
        }
        
        // Get source text content
        AcDbObjectId sourceId;
        if (!GetObjectId(sourceId, sourceEnt))
        {
            acutPrintf(_T("\nError: Could not get source entity ID.\n"));
            return;
        }
        
        CString sourceText;
        if (!GetText(sourceId, sourceText))
        {
            acutPrintf(_T("\nError: Selected entity is not a text object.\n"));
            return;
        }

        acutPrintf(_T("Source text: \"%s\"\n"), (LPCTSTR)sourceText);
        acutPrintf(_T("\nSelect destination text objects...\n"));

        if (!helper.SelectDestinations())
        {
            helper.PrintNoDestinations();
            return;
        }

        std::vector<AcDbObjectId> destIds = CommonTools::SelectionIds(helper.GetDestinationSet());
        acutPrintf(_T("Selected %d destination objects.\n"), static_cast<int>(destIds.size()));

        int updatedCount = 0;
        int skippedCount = 0;
        for (AcDbObjectId objId : destIds)
        {
            if (objId != sourceId && SetText(objId, sourceText)) updatedCount++;
            else                                                 skippedCount++;
        }

        acutPrintf(_T("\nCopy complete: %d text objects updated, %d skipped\n"), updatedCount, skippedCount);
    }
}

// ============================================================================
// CopyTextStyleCommand: shared body for COPYSTYLE / COPYTEXTFULL.
// includeHeight=false → style-only;  includeHeight=true → style + height.
// CC=5  CogC=5  Nesting=2
// ============================================================================
namespace TextTools {
    static void CopyTextStyleCommand(bool includeHeight)
    {
        const TCHAR* header = includeHeight ? _T("\n=== COPY TEXT STYLE + DIMENSIONS ===\n")
                                            : _T("\n=== COPY TEXT STYLE ===\n");
        acutPrintf(header);

        CommandHelper helper(includeHeight ? _T("COPYTEXTFULL") : _T("COPYSTYLE"));

        ads_name sourceEnt;
        if (!helper.SelectSource(sourceEnt)) { helper.PrintCommandCancelled(); return; }
        AcDbObjectId sourceId;
        if (!GetObjectId(sourceId, sourceEnt)) { helper.PrintSourceIdError(); return; }

        {
            CommonTools::AcDbObjectGuard<AcDbObject> pSrc(sourceId);
            if (!pSrc) { helper.PrintSourceOpenError(); return; }
            if (GetTextType(pSrc.get(), nullptr, nullptr) == TextType_None)
            { acutPrintf(_T("\nError: Source entity is not a text object.\n")); return; }
        }

        if (!helper.SelectDestinations()) { helper.PrintNoDestinations(); return; }

        std::vector<AcDbObjectId> destIds = CommonTools::SelectionIds(helper.GetDestinationSet());
        acutPrintf(_T("Processing %d destination object(s)...\n"), static_cast<int>(destIds.size()));

        int skipped = 0;
        int updated = CopyTextStyle(sourceId, destIds, includeHeight, &skipped);
        acutPrintf(_T("\nUpdated: %d | Skipped: %d\n"), updated, skipped);
    }
}

// COPYSTYLE — style only (no height)
void TextTools::copyStyleCommand()   { TextTools::CopyTextStyleCommand(false); }

// COPYTEXTFULL — style + height
void TextTools::copyTextFullCommand() { TextTools::CopyTextStyleCommand(true); }
// COPYDIMSTYLE command - Copy dimension style from one dimension to others.
// CC=5  CogC=5  Nesting=2
void TextTools::copyDimStyleCommand()
{
    CommandHelper helper(_T("COPYDIMSTYLE"));
    acutPrintf(_T("\n=== COPY DIMENSION STYLE ===\n"));

    ads_name sourceEnt;
    if (!helper.SelectSource(sourceEnt)) { helper.PrintCommandCancelled(); return; }
    AcDbObjectId sourceId;
    if (!GetObjectId(sourceId, sourceEnt)) { helper.PrintSourceIdError(); return; }

    CommonTools::AcDbObjectGuard<AcDbObject> pSrc(sourceId);
    if (!pSrc) { helper.PrintSourceOpenError(); return; }

    AcDbDimension* pSrcDim = AcDbDimension::cast(pSrc.get());
    if (!pSrcDim)
    { acutPrintf(_T("\nError: Source entity is not a dimension.\n")); return; }

    AcDbObjectId dimStyleId = pSrcDim->dimensionStyle();

    TCHAR styleName[256] = _T("Unknown");
    {
        CommonTools::AcDbObjectGuard<AcDbDimStyleTableRecord> dsr(dimStyleId);
        if (dsr) { const TCHAR* n = nullptr; if (dsr->getName(n) == Acad::eOk && n) _tcscpy_s(styleName, 256, n); }
    }
    acutPrintf(_T("Source dimension style: %s\n"), styleName);

    if (!helper.SelectDestinations()) { helper.PrintNoDestinations(); return; }

    std::vector<AcDbObjectId> destIds = CommonTools::SelectionIds(helper.GetDestinationSet());
    acutPrintf(_T("Processing %d destination object(s)...\n"), static_cast<int>(destIds.size()));

    int skipped = 0;
    int updated = CopyDimStyle(sourceId, destIds, &skipped);

    acutPrintf(_T("\nUpdated: %d | Skipped: %d\n"), updated, skipped);
}

// ============================================================================
// SUMTEXT Command - Sum numeric values from selected text objects
// ============================================================================

void TextTools::sumTextCommand()
{
    acutPrintf(_T("\n=== SUM TEXT VALUES ===\n"));
    acutPrintf(_T("Select text objects containing numeric values:\n"));
    
    std::vector<AcDbObjectId> ids = CommonTools::SelectIds(_T("TEXT,MTEXT"));
    if (ids.empty())
    {
        acutPrintf(_T("No objects selected.\n"));
        return;
    }
    acutPrintf(_T("Processing %d text object(s)...\n"), static_cast<int>(ids.size()));

    int validCount = 0, invalidCount = 0;
    double totalSum = SumTextValues(ids, &validCount, &invalidCount, true);

    if (validCount == 0)
    {
        acutPrintf(_T("\nNo valid numeric values found.\n"));
        return;
    }
    
    acutPrintf(_T("\n========================================\n"));
    acutPrintf(_T("TOTAL SUM: %.2f\n"), totalSum);
    acutPrintf(_T("Valid values: %d\n"), validCount);
    acutPrintf(_T("Skipped: %d\n"), invalidCount);
    acutPrintf(_T("========================================\n"));
    
    // Ask user for insertion point
    acutPrintf(_T("\nSpecify point for sum text:\n"));
    
    ads_point insertPt;
    if (acedGetPoint(NULL, _T("Insertion point: "), insertPt) != RTNORM)
    {
        acutPrintf(_T("\nCommand cancelled. Sum calculated but not inserted.\n"));
        return;
    }
    
    // Convert to AcGePoint3d
    AcGePoint3d position(insertPt[X], insertPt[Y], insertPt[Z]);
    
    // Format sum value
    CString sumText;
    sumText.Format(_T("%.2f"), totalSum);
    
    // Get current text style and height from database
    AcDbDatabase* pDb = acdbHostApplicationServices()->workingDatabase();
    if (!pDb)
    {
        acutPrintf(_T("Error: No active database.\n"));
        return;
    }
    
    AcDbObjectId textStyleId = pDb->textstyle();
    // Same height rule as every other label (style height, TEXTSIZE, unit minimum).
    double textHeight = CadInfra::ResolveTextHeight(pDb);

    // Create new text entity
    AcDbText* pNewText = new AcDbText();
    pNewText->setPosition(position);
    pNewText->setHeight(textHeight);
    pNewText->setTextString(sumText);
    pNewText->setTextStyle(textStyleId);
    
    if (CommonTools::AppendToModelSpace(pNewText).isNull())
        acutPrintf(_T("\nError: Could not add text to drawing.\n"));
    else
        acutPrintf(_T("\nSum text created: %s\n"), (LPCTSTR)sumText);
}

// SCALETEXT command - Scale text height of selected objects by a factor
void TextTools::scaleTextCommand()
{
    acutPrintf(_T("\n=== SCALE TEXT HEIGHT ===\n"));

    // Get scale factor
    double factor = 1.0;
    if (acedGetReal(_T("\nScale factor (e.g. 2.0 to double, 0.5 to halve): "), &factor) != RTNORM)
    { acutPrintf(_T("\nCommand cancelled.\n")); return; }
    if (factor <= 0.0)
    { acutPrintf(_T("\nError: Scale factor must be > 0.\n")); return; }

    std::vector<AcDbObjectId> ids = CommonTools::SelectIds();
    if (ids.empty()) { acutPrintf(_T("\nNo objects selected.\n")); return; }

    int count = ScaleTextHeight(ids, factor);
    acutPrintf(_T("\nScaled text height x%.2f on %d object(s).\n"), factor, count);
}

// ============================================================================
// Non-interactive cores (used by the commands above and the Lua bindings)
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
