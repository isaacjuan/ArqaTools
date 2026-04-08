// TextTools.cpp - Text Manipulation Tools Implementation

#include "StdAfx.h"
#include "HelloWorld.h"
#include "TextTools.h"
#include "CommonTools.h"
#include "dbmtext.h"
#include "dbdim.h"

namespace TextTools
{
    // ============================================================================
    // CONSTANTS
    // ============================================================================
    
    // Maximum text buffer size (characters)
    const size_t MAX_TEXT_BUFFER = 2048;

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

    // Check if entity is a text object (AcDbText or AcDbMText)
    static bool IsTextEntity(AcDbEntity* pEnt)
    {
        return GetTextType(pEnt, nullptr, nullptr) != TextType_None;
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
    
    // Get text length without extracting content (for validation)
    static size_t GetTextLength(AcDbEntity* pEnt)
    {
        if (!pEnt)
            return 0;

        AcDbText* pText = nullptr;
        AcDbMText* pMText = nullptr;
        TextType type = GetTextType(pEnt, &pText, &pMText);

        const TCHAR* textStr = nullptr;

        switch (type)
        {
        case TextType_DbText:
            textStr = pText ? pText->textString() : nullptr;
            break;

        case TextType_MText:
            textStr = pMText ? pMText->contents() : nullptr;
            break;

        default:
            return 0;
        }

        return textStr ? _tcslen(textStr) : 0;
    }

    // Extract text content from a text entity into buffer
    // Supports both AcDbText and AcDbMText
    static bool GetTextContent(AcDbEntity* pEnt, TCHAR* buffer, size_t bufferSize)
    {
        if (!pEnt || !buffer)
            return false;

        AcDbText* pText = nullptr;
        AcDbMText* pMText = nullptr;
        TextType type = GetTextType(pEnt, &pText, &pMText);

        const TCHAR* textStr = nullptr;

        // Extract text string based on type
        switch (type)
        {
        case TextType_DbText:
            textStr = pText ? pText->textString() : nullptr;
            break;

        case TextType_MText:
            textStr = pMText ? pMText->contents() : nullptr;
            break;

        default:
            return false;
        }

        // Copy text to buffer if valid with length check
        if (textStr)
        {
            size_t textLen = _tcslen(textStr);
            
            // Validate length before copying
            if (textLen >= bufferSize)
            {
                acutPrintf(_T("\nWarning: Text content exceeds buffer size (%d chars). Truncating...\n"), textLen);
                // Copy what we can (truncated)
                _tcsncpy_s(buffer, bufferSize, textStr, bufferSize - 1);
                buffer[bufferSize - 1] = _T('\0');
                return true; // Still return true but with truncated content
            }
            
            _tcscpy_s(buffer, bufferSize, textStr);
            return true;
        }

        return false;
    }

    // Set text content to a text entity
    // Supports both AcDbText and AcDbMText
    static bool SetTextContent(AcDbEntity* pEnt, const TCHAR* content)
    {
        if (!pEnt || !content)
            return false;

        // Validate content length (informational only - AutoCAD handles large text)
        size_t contentLen = _tcslen(content);
        if (contentLen >= MAX_TEXT_BUFFER)
        {
            acutPrintf(_T("  Info: Setting text with %d characters\n"), contentLen);
        }

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
    
    // Select a single entity with user prompt
    static bool SelectSourceEntity(ads_name& ent, const TCHAR* prompt = _T("\nSelect source text: "))
    {
        ads_point pickPt;
        return acedEntSel(prompt, ent, pickPt) == RTNORM;
    }

    // Select multiple entities (returns selection set)
    static bool SelectDestinationEntities(ads_name& ss)
    {
        return acedSSGet(nullptr, nullptr, nullptr, nullptr, ss) == RTNORM;
    }

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
        
        CommonTools::AcDbObjectGuard<AcDbEntity> srcEnt(sourceId);
        if (!srcEnt) { acutPrintf(_T("\nError: Could not open source entity.\n")); return; }
        AcDbEntity* pSourceEnt = srcEnt.get();
        
        // Check text length before extraction
        size_t textLength = GetTextLength(pSourceEnt);
        if (textLength >= MAX_TEXT_BUFFER)
        {
            acutPrintf(_T("\nWarning: Source text is %d characters (max %d). Text will be truncated.\n"), 
                      textLength, MAX_TEXT_BUFFER - 1);
        }
        
        TCHAR sourceText[MAX_TEXT_BUFFER] = {0};
        bool isValidSource = GetTextContent(pSourceEnt, sourceText, MAX_TEXT_BUFFER);
        
        if (!isValidSource)
        {
            acutPrintf(_T("\nError: Selected entity is not a text object.\n"));
            return;
        }
        
        acutPrintf(_T("Source text: \"%s\"\n"), sourceText);
        acutPrintf(_T("\nSelect destination text objects...\n"));
        
        // Select destination text entities
        if (!helper.SelectDestinations())
        {
            helper.PrintNoDestinations();
            return;
        }
        
        // Get selection count
        const ads_name& ss = helper.GetDestinationSet();
        Adesk::Int32 length;
        acedSSLength(ss, &length);
        acutPrintf(_T("Selected %d destination objects.\n"), length);
        
        int updatedCount = 0;
        int skippedCount = 0;
        
        // Process each selected destination
        CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId objId)
        {
            if (objId == sourceId) { skippedCount++; return; }

            CommonTools::AcDbObjectGuard<AcDbEntity> pEnt(objId, AcDb::kForWrite);
            if (!pEnt) { skippedCount++; return; }

            if (SetTextContent(pEnt.get(), sourceText))
                updatedCount++;
            else
                skippedCount++;
        });

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

        CommonTools::AcDbObjectGuard<AcDbObject> pSrc(sourceId);
        if (!pSrc) { helper.PrintSourceOpenError(); return; }

        AcDbObjectId styleId;
        double height = 0.0, widthFactor = 1.0, oblique = 0.0;
        AcDb::TextHorzMode horzMode = AcDb::kTextLeft;
        AcDb::TextVertMode vertMode = AcDb::kTextBase;

        AcDbText* pST = nullptr; AcDbMText* pSMT = nullptr;
        TextType srcType = GetTextType(pSrc.get(), &pST, &pSMT);

        if (srcType == TextType_DbText && pST)
        {
            styleId = pST->textStyle(); height = pST->height();
            widthFactor = pST->widthFactor(); oblique = pST->oblique();
            horzMode = pST->horizontalMode(); vertMode = pST->verticalMode();
        }
        else if (srcType == TextType_MText && pSMT)
        { styleId = pSMT->textStyle(); height = pSMT->textHeight(); }
        else
        { acutPrintf(_T("\nError: Source entity is not a text object.\n")); return; }


        if (!helper.SelectDestinations()) { helper.PrintNoDestinations(); return; }

        const ads_name& ss = helper.GetDestinationSet();
        Adesk::Int32 length = 0;
        acedSSLength(ss, &length);
        acutPrintf(_T("Processing %d destination object(s)...\n"), length);

        int updated = 0, skipped = 0;
        CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId destId)
        {
            if (destId == sourceId) { skipped++; return; }

            CommonTools::AcDbObjectGuard<AcDbObject> pDest(destId, AcDb::kForWrite);
            if (!pDest) { skipped++; return; }

            if (IsDimensionEntity(pDest.get())) { skipped++; return; }

            AcDbText* pDT = nullptr; AcDbMText* pDMT = nullptr;
            TextType dt = GetTextType(pDest.get(), &pDT, &pDMT);
            bool ok = false;

            if (dt == TextType_DbText && pDT)
            {
                pDT->setTextStyle(styleId);
                if (includeHeight) pDT->setHeight(height);
                pDT->setWidthFactor(widthFactor);
                pDT->setOblique(oblique);
                pDT->setHorizontalMode(horzMode);
                pDT->setVerticalMode(vertMode);
                ok = true;
            }
            else if (dt == TextType_MText && pDMT)
            {
                pDMT->setTextStyle(styleId);
                if (includeHeight) pDMT->setTextHeight(height);
                ok = true;
            }

            if (ok) updated++; else skipped++;
        });

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

    const ads_name& ss = helper.GetDestinationSet();
    Adesk::Int32 length = 0;
    acedSSLength(ss, &length);
    acutPrintf(_T("Processing %d destination object(s)...\n"), length);

    int updated = 0, skipped = 0;
    CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId destId)
    {
        if (destId == sourceId) { skipped++; return; }

        CommonTools::AcDbObjectGuard<AcDbObject> pDest(destId, AcDb::kForWrite);
        if (!pDest) { skipped++; return; }

        AcDbDimension* pDim = AcDbDimension::cast(pDest.get());
        if (pDim) { pDim->setDimensionStyle(dimStyleId); updated++; }
        else      { skipped++; }
    });

    acutPrintf(_T("\nUpdated: %d | Skipped: %d\n"), updated, skipped);
}

// ============================================================================
// SUMTEXT Command - Sum numeric values from selected text objects
// ============================================================================

void TextTools::sumTextCommand()
{
    acutPrintf(_T("\n=== SUM TEXT VALUES ===\n"));
    acutPrintf(_T("Select text objects containing numeric values:\n"));
    
    // Select text objects
    ads_name ss;
    struct resbuf* filter = acutBuildList(
        RTDXF0, _T("TEXT,MTEXT"),
        RTNONE
    );
    
    if (acedSSGet(NULL, NULL, NULL, filter, ss) != RTNORM)
    {
        acutRelRb(filter);
        acutPrintf(_T("No objects selected.\n"));
        return;
    }
    acutRelRb(filter);
    
    // Get selection set length
    Adesk::Int32 length = 0;
    acedSSLength(ss, &length);
    
    if (length == 0)
    {
        acedSSFree(ss);
        acutPrintf(_T("Selection set is empty.\n"));
        return;
    }
    
    acutPrintf(_T("Processing %d text object(s)...\n"), length);
    
    double totalSum = 0.0;
    int validCount = 0;
    int invalidCount = 0;
    
    // Process each text object
    CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId objId)
    {
        CommonTools::AcDbObjectGuard<AcDbObject> pObj(objId);
        if (!pObj) return;

        CString textContent;
        AcDbText*  pText  = AcDbText::cast(pObj.get());
        AcDbMText* pMText = pText ? nullptr : AcDbMText::cast(pObj.get());
        if      (pText)  textContent = pText->textString();
        else if (pMText) textContent = pMText->contents();
        else             return;

        textContent.Trim();
        textContent.Replace(_T("$"), _T(""));
        textContent.Replace(_T("€"), _T(""));
        textContent.Replace(_T("£"), _T(""));
        textContent.Replace(_T(" "), _T(""));
        textContent.Replace(_T(","), _T(""));

        if (textContent.IsEmpty()) { invalidCount++; return; }

        TCHAR* endPtr = nullptr;
        double value = _tcstod(textContent, &endPtr);
        if (endPtr != nullptr && (*endPtr == _T('\0') || *endPtr == _T('\n')))
        { totalSum += value; validCount++; acutPrintf(_T("  %s = %.2f\n"), (LPCTSTR)textContent, value); }
        else
        { invalidCount++; acutPrintf(_T("  Skipped '%s' (not numeric)\n"), (LPCTSTR)textContent); }
    });

    acedSSFree(ss);
    
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
    double textHeight = 2.5;  // Default height
    
    // Try to get TEXTSIZE system variable
    struct resbuf rb;
    if (acedGetVar(_T("TEXTSIZE"), &rb) == RTNORM)
    {
        textHeight = rb.resval.rreal;
    }
    
    // Create new text entity
    AcDbText* pNewText = new AcDbText();
    pNewText->setPosition(position);
    pNewText->setHeight(textHeight);
    pNewText->setTextString(sumText);
    pNewText->setTextStyle(textStyleId);
    
    // Add to model space
    AcDbBlockTable* pBlockTable = nullptr;
    if (pDb->getBlockTable(pBlockTable, AcDb::kForRead) == Acad::eOk)
    {
        AcDbBlockTableRecord* pModelSpace = nullptr;
        if (pBlockTable->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForWrite) == Acad::eOk)
        {
            AcDbObjectId newTextId;
            if (pModelSpace->appendAcDbEntity(newTextId, pNewText) == Acad::eOk)
            {
                acutPrintf(_T("\nSum text created: %s\n"), (LPCTSTR)sumText);
                pNewText->close();
            }
            else
            {
                acutPrintf(_T("\nError: Could not add text to drawing.\n"));
                delete pNewText;
            }
            pModelSpace->close();
        }
        else
        {
            acutPrintf(_T("\nError: Could not open model space.\n"));
            delete pNewText;
        }
        pBlockTable->close();
    }
    else
    {
        acutPrintf(_T("\nError: Could not access block table.\n"));
        delete pNewText;
    }
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

    // Select text objects
    ads_name ss;
    if (acedSSGet(NULL, NULL, NULL, NULL, ss) != RTNORM)
    { acutPrintf(_T("\nNo objects selected.\n")); return; }

    Adesk::Int32 length = 0;
    acedSSLength(ss, &length);

    int count = 0;
    CommonTools::ForEachSsEntity(ss, length, [&](AcDbObjectId objId)
    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(objId, AcDb::kForWrite);
        if (!ent) return;

        if (ent->isKindOf(AcDbText::desc()))
        { static_cast<AcDbText*>(ent.get())->setHeight(static_cast<AcDbText*>(ent.get())->height() * factor); count++; }
        else if (ent->isKindOf(AcDbMText::desc()))
        { static_cast<AcDbMText*>(ent.get())->setTextHeight(static_cast<AcDbMText*>(ent.get())->textHeight() * factor); count++; }
        // Note: AcDbDimension text height is controlled by dim style
    });

    acedSSFree(ss);
    acutPrintf(_T("\nScaled text height x%.2f on %d object(s).\n"), factor, count);
}