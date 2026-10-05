// CommandTester.h - behavior check for AI-written Lua commands (ATAICMD)
//
// The static checks in LuaCommands (API names, a trial load, the definition)
// never run the command's function, so they cannot tell whether it does what
// was asked. CommandTester runs it once:
//
//   1. in a scratch, in-memory drawing (the user's drawing is never touched),
//      with test values for its declared parameters (defaults, else a
//      plausible value per type - see sample in LuaTools' prelude);
//   2. reads back what it drew into a text report (counts, extents, every
//      entity's geometry, and mechanical defects: zero-length segments,
//      segments drawn back over themselves, duplicated lines, ...);
//   3. renders a plan view PNG (GDI+) of the result, which the AI reviewer
//      looks at together with the report (AITools::SendWithImage).
//
// Commands that need existing objects (entity/selection parameters) or that
// declare no parameters are not test-run; the result says why.

#pragma once
#include "StdAfx.h"
#include <string>

namespace CommandTester
{
    struct Result
    {
        bool        ran = false;      // the command function was executed
        bool        ok  = false;      // ... and finished without a Lua error
        CString     skipped;          // why it was not (fully) test-run; empty when it was
        std::string params;           // test parameter values (Lua table constructor)
        std::string output;           // what it printed
        std::string error;            // runtime error with traceback, when !ok
        std::string report;           // geometry report of what it drew
        CString     pngPath;          // rendered plan view; empty when nothing was drawn/rendered
    };

    // `code` is a command file already accepted by LuaCommands' validation
    // that defines `name`. The PNG is written to pngPath.
    Result Run(const CString& name, const CString& code, const CString& pngPath);
}
