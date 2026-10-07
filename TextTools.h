// TextTools.h - Text Manipulation Tools Header
//
// The commands (ATCOPYTEXT, ATCOPYSTYLE, ATCOPYTEXTFULL, ATCOPYDIMSTYLE,
// ATSUMTEXT, ATSCALETEXT) are Lua files in LuaCommands\; these are the
// non-interactive cores behind their at.* functions.

#pragma once

#include "StdAfx.h"
#include <vector>

namespace TextTools
{
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
}
