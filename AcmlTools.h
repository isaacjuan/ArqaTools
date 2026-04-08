// AcmlTools.h — ACML interpreter commands
//
// Registers the following AutoCAD commands:
//   ACML        — load and run an .acml file (full pipeline, Phase 3+)
//   ACMLCHECK   — lex + parse and report errors without drawing
//   ACMLLEX     — lex only; dumps token stream to command line (diagnostic)

#pragma once
#include "StdAfx.h"

namespace AcmlTools
{
    // ACML   — prompt for .acml file, run full pipeline
    void acmlRunCommand();

    // ACMLCHECK — prompt for .acml file, lex + report errors/warnings only
    void acmlCheckCommand();

    // ACMLLEX — prompt for .acml file, tokenize and print token stream
    void acmlLexCommand();

} // namespace AcmlTools
