// TextTools.h - Text Manipulation Tools Header

#pragma once

#include "StdAfx.h"
#include <vector>

namespace TextTools
{
    // Command: Copy text content from one text object to others
    void copyTextCommand();
    
    // Command: Copy text style properties from one text object to others
    void copyStyleCommand();
    
    // Command: Copy text style AND dimensions (height) from one text to others
    void copyTextFullCommand();
    
    // Command: Copy dimension style from one dimension to others
    void copyDimStyleCommand();
    
    // Command: Sum numeric values from selected text objects
    void sumTextCommand();

    // Command: Scale text height of selected text objects by a ratio
    void scaleTextCommand();

    // ── Non-interactive cores (used by the commands above and the Lua bindings).
    // GetText/SetText work on AcDbText and AcDbMText (MText keeps its format codes).
    bool GetText(AcDbObjectId id, CString& out);
    bool SetText(AcDbObjectId id, const CString& text);
    // Copy style (and optionally height) from srcId to every text in destIds.
    // Returns the number updated, or -1 if srcId is not text.
    int CopyTextStyle(AcDbObjectId srcId, const std::vector<AcDbObjectId>& destIds,
                      bool includeHeight, int* skipped = nullptr);
    // Returns the number of dimensions updated, or -1 if srcId is not a dimension.
    int CopyDimStyle(AcDbObjectId srcId, const std::vector<AcDbObjectId>& destIds,
                     int* skipped = nullptr);
    // Parses "1,234.50", "$12", "€ 3" ... -> number.
    bool ParseNumber(CString text, double& value);
    // Sums the numeric texts among ids; non-text objects are ignored.
    double SumTextValues(const std::vector<AcDbObjectId>& ids, int* valid = nullptr,
                         int* invalid = nullptr, bool verbose = true);
    int ScaleTextHeight(const std::vector<AcDbObjectId>& ids, double factor);

    // ============================================================================
    // INTERNAL HELPER CLASS (for optimization)
    // ============================================================================
    
    // Helper class to encapsulate common command operations
    // Reduces code duplication and optimizes memory usage with const string members
    class CommandHelper
    {
    public:
        // Constructor
        CommandHelper(const TCHAR* commandName);

        // Destructor - ensures selection set cleanup
        ~CommandHelper();

        // Select source entity
        bool SelectSource(ads_name& sourceEnt);

        // Select destination entities (returns selection set)
        bool SelectDestinations();

        // Get destination selection set (returns pointer for array access)
        const ads_name& GetDestinationSet() const { return m_destSelection; }

        // Print error messages (optimized with member strings)
        void PrintCommandCancelled() const;
        void PrintSourceIdError() const;
        void PrintSourceOpenError() const;
        void PrintNoDestinations() const;

    private:
        const TCHAR* m_commandName;
        ads_name m_destSelection;
        bool m_hasSelection;

        // Cached error message strings (memory optimization)
        static const TCHAR* const MSG_CANCELLED;
        static const TCHAR* const MSG_SOURCE_ID_ERROR;
        static const TCHAR* const MSG_SOURCE_OPEN_ERROR;
        static const TCHAR* const MSG_NO_DESTINATIONS;
    };
}
