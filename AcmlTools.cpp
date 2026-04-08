// AcmlTools.cpp — ACML interpreter command implementations
//
// Phase 2: ACMLLEX tokenizes and dumps the stream.
//          ACMLCHECK lexes + parses and reports errors + AST summary.
//          ACML is a stub (parser OK, geometry generator Phase 3).
#include "StdAfx.h"
#include "AcmlTools.h"
#include "AcmlLexer.h"
#include "AcmlParser.h"
#include "AcmlSemantic.h"
#include "AcmlGenerator.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

// ============================================================================
// Internal helpers
// ============================================================================
namespace
{
    // -------------------------------------------------------------------------
    // Registry persistence for the ACML recent-files list.
    // Key: HKCU\Software\HelloWorldAcad\ACML
    // Values: RecentFile0 … RecentFile(kMaxHistory-1)  (REG_SZ)
    // -------------------------------------------------------------------------
    static const TCHAR* kRegKey = _T("Software\\HelloWorldAcad\\ACML");
    constexpr int kMaxHistory = 5;

    void saveHistory(const std::vector<CString>& history)
    {
        HKEY hKey = nullptr;
        if (RegCreateKeyEx(HKEY_CURRENT_USER, kRegKey, 0, nullptr,
                           REG_OPTION_NON_VOLATILE, KEY_WRITE,
                           nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            return;

        for (int i = 0; i < kMaxHistory; ++i)
        {
            TCHAR valueName[16];
            _stprintf_s(valueName, _T("RecentFile%d"), i);
            if (i < (int)history.size())
            {
                const CString& path = history[i];
                RegSetValueEx(hKey, valueName, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>((LPCTSTR)path),
                              (path.GetLength() + 1) * sizeof(TCHAR));
            }
            else
            {
                // Clear slots beyond current list length
                RegDeleteValue(hKey, valueName);
            }
        }
        RegCloseKey(hKey);
    }

    void loadHistory(std::vector<CString>& history)
    {
        history.clear();
        HKEY hKey = nullptr;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, kRegKey, 0,
                         KEY_READ, &hKey) != ERROR_SUCCESS)
            return;

        for (int i = 0; i < kMaxHistory; ++i)
        {
            TCHAR valueName[16];
            _stprintf_s(valueName, _T("RecentFile%d"), i);

            TCHAR buf[MAX_PATH] = {};
            DWORD size = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueEx(hKey, valueName, nullptr, &type,
                                reinterpret_cast<BYTE*>(buf),
                                &size) == ERROR_SUCCESS
                && type == REG_SZ && buf[0] != _T('\0'))
            {
                history.push_back(CString(buf));
            }
        }
        RegCloseKey(hKey);
    }


    // -------------------------------------------------------------------------
    // Prompt the user for a file path via acedGetString.
    // If defaultPath is non-empty it is shown as <default> and pressing Enter
    // reuses it.  Returns false (and prints a message) if cancelled.
    // -------------------------------------------------------------------------
    bool promptFilePath(CString& pathOut, const CString& defaultPath = CString())
    {
        TCHAR buf[MAX_PATH] = {};
        if (defaultPath.IsEmpty())
            acutPrintf(_T("\nACML file path: "));
        else
            acutPrintf(_T("\nACML file path <%s>: "), (LPCTSTR)defaultPath);

        int rc = acedGetString(/*allow spaces=*/1, _T(""), buf, MAX_PATH);
        if (rc != RTNORM)
        {
            acutPrintf(_T("\nCancelled.\n"));
            return false;
        }
        CString input(buf);
        input.Trim();
        if (input.IsEmpty())
        {
            if (defaultPath.IsEmpty())
            {
                acutPrintf(_T("\nCancelled.\n"));
                return false;
            }
            pathOut = defaultPath;  // Enter/Space with no input → reuse last path
        }
        else
        {
            pathOut = input;
        }
        return true;
    }

    // -------------------------------------------------------------------------
    // Read a UTF-8 text file into a std::string.
    // Returns false if the file cannot be opened.
    // -------------------------------------------------------------------------
    bool readFile(const CString& path, std::string& contentsOut)
    {
        // Convert CString (UTF-16) to narrow path for std::ifstream
        CT2A narrowPath(path, CP_UTF8);
        std::ifstream f(static_cast<const char*>(narrowPath),
                        std::ios::in | std::ios::binary);
        if (!f.is_open()) return false;

        std::ostringstream ss;
        ss << f.rdbuf();
        contentsOut = ss.str();
        return true;
    }

    // -------------------------------------------------------------------------
    // Return a short label for a TokenType (for diagnostic output).
    // -------------------------------------------------------------------------
    const char* tokenLabel(Acml::TokenType t)
    {
        using T = Acml::TokenType;
        switch (t)
        {
        case T::LBrace:      return "LBRACE";
        case T::RBrace:      return "RBRACE";
        case T::LParen:      return "LPAREN";
        case T::RParen:      return "RPAREN";
        case T::LBracket:    return "LBRACKET";
        case T::RBracket:    return "RBRACKET";
        case T::Colon:       return "COLON";
        case T::Semicolon:   return "SEMICOL";
        case T::Comma:       return "COMMA";
        case T::Dot:         return "DOT";
        case T::Newline:     return "NL";
        case T::ElementType: return "ELEM";
        case T::Identifier:  return "IDENT";
        case T::Number:      return "NUM";
        case T::String:      return "STR";
        case T::ColorHex:    return "COLOR";
        case T::Plus:        return "PLUS";
        case T::Minus:       return "MINUS";
        case T::Star:        return "STAR";
        case T::Slash:       return "SLASH";
        case T::Percent:     return "PCT";
        case T::EqEq:        return "EQEQ";
        case T::NotEq:       return "NOTEQ";
        case T::Lt:          return "LT";
        case T::Gt:          return "GT";
        case T::LtEq:        return "LTEQ";
        case T::GtEq:        return "GTEQ";
        case T::AmpAmp:      return "AND";
        case T::PipePipe:    return "OR";
        case T::Eof:         return "EOF";
        case T::Error:       return "ERR";
        default:             return "?";
        }
    }

    // -------------------------------------------------------------------------
    // Prompt for a file path with a numbered recent-files list.
    // - Enter with no input  → reuse item 1 (most recent).
    // - A digit 1..N         → select that history entry.
    // - Any other text       → use as a new path.
    // Returns false on Escape / cancel.
    // history is updated by the caller after a successful selection.
    // -------------------------------------------------------------------------
    bool promptFilePathWithHistory(CString& pathOut,
                                   const std::vector<CString>& history)
    {
        if (!history.empty())
        {
            acutPrintf(_T("\nRecent ACML files:\n"));
            for (int i = 0; i < (int)history.size(); ++i)
                acutPrintf(_T("  %d. %s\n"), i + 1, (LPCTSTR)history[i]);
            acutPrintf(_T("File path or number <1>: "));
        }
        else
        {
            acutPrintf(_T("\nACML file path: "));
        }

        TCHAR buf[MAX_PATH] = {};
        int rc = acedGetString(/*allow spaces=*/1, _T(""), buf, MAX_PATH);
        if (rc != RTNORM)
        {
            acutPrintf(_T("\nCancelled.\n"));
            return false;
        }

        // Trim whitespace — Space bar is AutoCAD's alternative to Enter,
        // but with spaces allowed in the string it lands in the buffer.
        // A pure-whitespace response is treated the same as an empty Enter.
        CString input(buf);
        input.Trim();

        if (input.IsEmpty())
        {
            // Enter (or Space) with no real input → most recent file
            if (history.empty())
            {
                acutPrintf(_T("\nCancelled.\n"));
                return false;
            }
            pathOut = history[0];
            return true;
        }

        // Single digit → pick by number
        if (input.GetLength() == 1 && input[0] >= _T('1') && input[0] <= _T('9'))
        {
            int idx = input[0] - _T('1');
            if (idx < (int)history.size())
            {
                pathOut = history[idx];
                return true;
            }
            acutPrintf(_T("\nNo item %c in history.\n"), (TCHAR)input[0]);
            return false;
        }

        pathOut = input;
        return true;
    }

    // -------------------------------------------------------------------------
    // Add path to the front of history, removing any duplicate, capped at kMaxHistory.
    // -------------------------------------------------------------------------
    void pushHistory(std::vector<CString>& history, const CString& path)
    {
        history.erase(std::remove(history.begin(), history.end(), path),
                      history.end());
        history.insert(history.begin(), path);
        if ((int)history.size() > kMaxHistory)
            history.resize(kMaxHistory);
    }

    // -------------------------------------------------------------------------
    // Build a PickProvider that delegates to AutoCAD's acedGetPoint /
    // acedGetAngle.  Defined here so all ObjectARX I/O stays in AcmlTools.
    // -------------------------------------------------------------------------
    Acml::PickProvider makePickProvider()
    {
        Acml::PickProvider p;

        p.getPoint = [](const std::string& prompt,
                        double& x, double& y, double& z) -> bool
        {
            CA2T wPrompt((prompt + ": ").c_str(), CP_UTF8);
            ads_point pt = {};
            if (acedGetPoint(nullptr, wPrompt, pt) != RTNORM) return false;
            x = pt[X]; y = pt[Y]; z = pt[Z];
            return true;
        };

        p.getDistance = [](const std::string& startPrompt,
                           const std::string& endPrompt,
                           double& dist) -> bool
        {
            CA2T w1((startPrompt + ": ").c_str(), CP_UTF8);
            ads_point p1 = {};
            if (acedGetPoint(nullptr, w1, p1) != RTNORM) return false;

            CA2T w2((endPrompt + ": ").c_str(), CP_UTF8);
            ads_point p2 = {};
            if (acedGetPoint(p1, w2, p2) != RTNORM) return false;

            double dx = p2[X] - p1[X], dy = p2[Y] - p1[Y], dz = p2[Z] - p1[Z];
            dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            return true;
        };

        p.getAngle = [](const std::string& prompt, double& angleRad) -> bool
        {
            CA2T wPrompt((prompt + ": ").c_str(), CP_UTF8);
            return acedGetAngle(nullptr, wPrompt, &angleRad) == RTNORM;
        };

        return p;
    }

} // anonymous namespace

// ============================================================================
// ACMLLEX — tokenize and dump token stream
// ============================================================================
void AcmlTools::acmlLexCommand()
{
    static CString s_lastPath;
    CString path;
    if (!promptFilePath(path, s_lastPath)) return;
    s_lastPath = path;

    std::string source;
    if (!readFile(path, source))
    {
        acutPrintf(_T("\nError: cannot open file '%s'.\n"), (LPCTSTR)path);
        return;
    }

    // Convert path to narrow for lexer filename
    CT2A narrowPath(path, CP_UTF8);
    Acml::AcmlLexer lexer(source, static_cast<const char*>(narrowPath));
    auto tokens = lexer.tokenize();

    // Print lex errors
    if (lexer.hasErrors())
    {
        acutPrintf(_T("\n--- Lex errors ---\n"));
        for (const auto& e : lexer.errors())
        {
            CA2T wideErr(e.c_str(), CP_UTF8);
            acutPrintf(_T("  %s\n"), (LPCTSTR)wideErr);
        }
    }

    // Print token stream (skip Newline tokens for brevity — they are noise)
    acutPrintf(_T("\n--- Token stream (%d tokens) ---\n"),
               (int)tokens.size());

    int printed = 0;
    for (const auto& tok : tokens)
    {
        if (tok.type == Acml::TokenType::Newline) continue;
        if (tok.type == Acml::TokenType::Eof)     break;

        CA2T wideVal(tok.value.c_str(), CP_UTF8);
        CA2T wideUnit(tok.unit.c_str(), CP_UTF8);

        if (!tok.unit.empty())
            acutPrintf(_T("  [%3d:%-3d] %-8S  %-20s  unit:%s\n"),
                       tok.line, tok.col,
                       tokenLabel(tok.type),
                       (LPCTSTR)wideVal,
                       (LPCTSTR)wideUnit);
        else
            acutPrintf(_T("  [%3d:%-3d] %-8S  %s\n"),
                       tok.line, tok.col,
                       tokenLabel(tok.type),
                       (LPCTSTR)wideVal);

        ++printed;
        // Pause every 40 tokens to avoid flooding the command line
        if (printed % 40 == 0)
        {
            TCHAR cont[8] = {};
            acutPrintf(_T("  -- press Enter for next 40, 'q' to stop --\n"));
            acedGetString(0, _T(""), cont, 8);
            if (cont[0] == _T('q') || cont[0] == _T('Q')) break;
        }
    }

    acutPrintf(_T("\nACMLLEX: %d meaningful tokens. %d lex error(s).\n"),
               printed, (int)lexer.errors().size());
}

// ============================================================================
// ACMLCHECK — lex + parse and report errors + AST summary
// ============================================================================
void AcmlTools::acmlCheckCommand()
{
    static CString s_lastPath;
    CString path;
    if (!promptFilePath(path, s_lastPath)) return;
    s_lastPath = path;

    std::string source;
    if (!readFile(path, source))
    {
        acutPrintf(_T("\nError: cannot open file '%s'.\n"), (LPCTSTR)path);
        return;
    }

    CT2A narrowPath(path, CP_UTF8);
    const char* fname = static_cast<const char*>(narrowPath);

    // ── Lex ──────────────────────────────────────────────────────────────────
    Acml::AcmlLexer lexer(source, fname);
    auto tokens = lexer.tokenize();

    if (lexer.hasErrors())
    {
        acutPrintf(_T("\nACMLCHECK - %d lex error(s):\n"),
                   (int)lexer.errors().size());
        for (const auto& e : lexer.errors())
        {
            CA2T w(e.c_str(), CP_UTF8);
            acutPrintf(_T("  %s\n"), (LPCTSTR)w);
        }
        return;
    }

    // ── Parse ─────────────────────────────────────────────────────────────────
    Acml::AcmlParser parser(std::move(tokens), fname);
    auto doc = parser.parse();

    if (parser.hasErrors())
    {
        acutPrintf(_T("\nACMLCHECK - %d parse error(s):\n"),
                   (int)parser.errors().size());
        for (const auto& e : parser.errors())
        {
            CA2T w(e.c_str(), CP_UTF8);
            acutPrintf(_T("  %s\n"), (LPCTSTR)w);
        }
        return;
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    // Count top-level items by kind
    int elements = 0, components = 0, imports = 0, typedefs = 0;
    for (const auto& item : doc->items)
    {
        switch (item->kind)
        {
        case Acml::NodeKind::Element:        ++elements;    break;
        case Acml::NodeKind::ComponentDef:   ++components;  break;
        case Acml::NodeKind::ImportStmt:     ++imports;     break;
        case Acml::NodeKind::ElementTypeDef: ++typedefs;    break;
        default: break;
        }
    }

    acutPrintf(_T("\nACMLCHECK: OK\n"));
    acutPrintf(_T("  imports: %d  components: %d  types: %d  elements: %d\n"),
               imports, components, typedefs, elements);

    // Print AST (limited to first 60 lines to avoid flooding)
    std::string ast = Acml::AcmlParser::printAst(doc.get());
    std::istringstream ss(ast);
    std::string line;
    int lineCount = 0;
    acutPrintf(_T("\n--- AST ---\n"));
    while (std::getline(ss, line) && lineCount < 60)
    {
        CA2T wLine(line.c_str(), CP_UTF8);
        acutPrintf(_T("%s\n"), (LPCTSTR)wLine);
        ++lineCount;
    }
    if (!ss.eof())
        acutPrintf(_T("  ... (truncated - full AST has more nodes)\n"));
}

// ============================================================================
// ACML — full pipeline (geometry generator Phase 3)
// ============================================================================
void AcmlTools::acmlRunCommand()
{
    // File history — loaded from registry on first use, saved after each run.
    static std::vector<CString> s_history;
    static bool s_loaded = false;
    if (!s_loaded) { loadHistory(s_history); s_loaded = true; }

    CString path;
    if (!promptFilePathWithHistory(path, s_history)) return;
    pushHistory(s_history, path);
    saveHistory(s_history);

    std::string source;
    if (!readFile(path, source))
    {
        acutPrintf(_T("\nError: cannot open file '%s'.\n"), (LPCTSTR)path);
        return;
    }

    CT2A narrowPath(path, CP_UTF8);
    const char* fname = static_cast<const char*>(narrowPath);

    // Lex
    Acml::AcmlLexer lexer(source, fname);
    auto tokens = lexer.tokenize();
    if (lexer.hasErrors())
    {
        acutPrintf(_T("\nACML: lex errors - run ACMLCHECK for details. Aborted.\n"));
        return;
    }

    // Parse
    Acml::AcmlParser parser(std::move(tokens), fname);
    auto doc = parser.parse();
    if (parser.hasErrors())
    {
        acutPrintf(_T("\nACML: parse errors - run ACMLCHECK for details. Aborted.\n"));
        return;
    }

    // Semantic analysis — supply interactive pick callbacks
    Acml::AcmlSemantic sem(fname, makePickProvider());
    auto elements = sem.analyze(doc.get());

    // Check for interactive pick cancellation (user pressed Escape)
    if (sem.wasPickCancelled())
    {
        acutPrintf(_T("\nACML: Interpretation cancelled by user.\n"));
        return;
    }

    if (sem.hasErrors())
    {
        acutPrintf(_T("\nACML: %d semantic error(s):\n"),
                   (int)sem.errors().size());
        for (const auto& e : sem.errors())
        {
            CA2T w(e.c_str(), CP_UTF8);
            acutPrintf(_T("  %s\n"), (LPCTSTR)w);
        }
        // Continue anyway — best-effort drawing
    }

    // Generate geometry
    Acml::AcmlGenerator gen;
    int drawn = gen.generate(elements);

    if (gen.hasErrors())
        for (const auto& e : gen.errors())
        {
            CA2T w(e.c_str(), CP_UTF8);
            acutPrintf(_T("  [generator] %s\n"), (LPCTSTR)w);
        }

    acutPrintf(_T("\nACML: done - %d entit%s drawn.\n"),
               drawn, drawn == 1 ? _T("y") : _T("ies"));
}
