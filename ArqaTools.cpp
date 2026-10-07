// ArqaTools.cpp - Modern AutoCAD 2025 ObjectARX Plugin using OMF
// Demonstrates ObjectARX Module Framework (OMF) with AcRxArxApp
//
// Commands:
//   BOOLPOLY - Boolean operations on polylines (union/intersection/subtract)

#include "StdAfx.h"
#include "ArqaTools.h"

// CAtlMfcModule: ATL module for MFC+ATL mixed DLLs — does NOT override DllMain,
// so MFC's DllMain correctly initializes afxCurrentResourceHandle.
class CArqaToolsAtlModule : public CAtlMfcModule {};
CArqaToolsAtlModule _AtlModule;

#ifdef _DEBUG
#include <crtdbg.h>
static bool g_assertsSuppressed = false;

static void suppressAssertsCommand()
{
    if (!g_assertsSuppressed)
    {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
        g_assertsSuppressed = true;
        acutPrintf(_T("\nAsserts suppressed (output to debugger only).\n"));
    }
    else
        acutPrintf(_T("\nAsserts already suppressed.\n"));
}

static void unsuppressAssertsCommand()
{
    if (g_assertsSuppressed)
    {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_WNDW);
        g_assertsSuppressed = false;
        acutPrintf(_T("\nAsserts restored (dialog enabled).\n"));
    }
    else
        acutPrintf(_T("\nAsserts are not suppressed.\n"));
}
#endif
#include "CommonTools.h"
#include "AreaTools.h"
#include "AITools.h"
#include "AcmlTools.h"
#include "LuaTools.h"
#include "LuaCommands.h"
#include "McpBridge.h"
#include "dbregion.h"
#include "dbgroup.h"

using namespace AITools;

// ============================================================================
// VERSION INFORMATION
// ============================================================================

// Build version string (changes with every compile)
const TCHAR* GetVersionString()
{
    static TCHAR versionBuffer[256];
    _stprintf_s(versionBuffer, 256, _T("v%d.%d - Build: %s %s"), 
                ARQATOOLS_VERSION_MAJOR, 
                ARQATOOLS_VERSION_MINOR,
                _T(ARQATOOLS_BUILD_DATE),
                _T(ARQATOOLS_BUILD_TIME));
    return versionBuffer;
}

// Constructor
CArqaToolsApp::CArqaToolsApp() : AcRxArxApp() 
{
}

// Initialize application
AcRx::AppRetCode CArqaToolsApp::On_kInitAppMsg(void* pAppData)
{
    AcRx::AppRetCode retCode = AcRxArxApp::On_kInitAppMsg(pAppData);

    static const struct { const TCHAR* name; AcRxFunctionPtr func; } kCommands[] =
    {
        // Core
        { _T("ATHELP"),             arqaHelpCommand             },
        { _T("ATVERSION"),          versionCommand              },
        { _T("ATRELOAD"),           reloadCommand               },
        // AI
        { _T("ATAIASK"),            aiAskCommand                },
        { _T("ATAISETTOKEN"),       aiSetTokenCommand           },
        { _T("ATAISETENDPOINT"),    aiSetEndpointCommand        },
        { _T("ATAISETMODEL"),       aiSetModelCommand           },
        { _T("ATAITEST"),           aiTestCommand               },
        { _T("ATAILISTMODELS"),     aiListModelsCommand         },
        { _T("ATAIDRAW"),           aiDrawCommand               },
        { _T("ATAIHELP"),           aiHelpCommand               },
        { _T("ATAILISP"),           aiLispCommand               },
        { _T("ATAIFIX"),            aiFixCommand                },
        { _T("ATAICLEAR"),          aiClearHistoryCommand       },
        { _T("ATAICONFIG"),         aiConfigCommand             },
        // ACML interpreter
        { _T("ATACML"),             AcmlTools::acmlRunCommand       },
        { _T("ATACMLCHECK"),        AcmlTools::acmlCheckCommand     },
        { _T("ATACMLLEX"),          AcmlTools::acmlLexCommand       },
        // Lua scripting (second, additive scripting engine alongside LISP/ACML)
        { _T("ATLUA"),              LuaTools::luaRunCommand      },
        { _T("ATAILUA"),            LuaTools::aiLuaCommand       },
        { _T("ATAICMD"),            LuaCommands::aiCommand       },
        { _T("ATLUACMDS"),          LuaCommands::listCommand     },
        { _T("ATLUARELOAD"),        LuaCommands::reloadCommand   },
        { _T("ATLUACMDDEL"),        LuaCommands::deleteCommand   },
        { _T("ATLUAFOLDER"),        LuaCommands::folderCommand   },
        // MCP bridge for ArqaToolsMcp.exe (ATMCPRUN is internal)
        { _T("ATMCPSTART"),         McpBridge::startCommand      },
        { _T("ATMCPSTOP"),          McpBridge::stopCommand       },
        { _T("ATMCPSTATUS"),        McpBridge::statusCommand     },
        { _T("ATMCPRUN"),           McpBridge::runCommand        },
    };

    for (const auto& cmd : kCommands)
        acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), cmd.name, cmd.name, ACRX_CMD_MODAL, cmd.func);

#ifdef _DEBUG
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("ATSUPPRESSASSERTS"),   _T("ATSUPPRESSASSERTS"),   ACRX_CMD_MODAL, suppressAssertsCommand);
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("ATUNSUPPRESSASSERTS"), _T("ATUNSUPPRESSASSERTS"), ACRX_CMD_MODAL, unsuppressAssertsCommand);
#endif

    InitAreaToolsPersistence();
    LuaCommands::Init();

    acutPrintf(_T("\n=== ArqaTools 2025 Plugin Loaded ===\n"));
    acutPrintf(_T("%s\n"), GetVersionString());
    acutPrintf(_T("Type ATHELP to see all available commands\n"));
    return retCode;
}

// Unload application
AcRx::AppRetCode CArqaToolsApp::On_kUnloadAppMsg(void* pAppData)
{
    AcRx::AppRetCode retCode = AcRxArxApp::On_kUnloadAppMsg(pAppData);

    UninitAreaToolsPersistence();
    McpBridge::Uninit();
    LuaCommands::Uninit();
    acedRegCmds->removeGroup(_T("ARQATOOLS_COMMANDS"));
    acutPrintf(_T("\nArqaTools plugin unloaded.\n"));
    
    return retCode;
}

// Register server components
void CArqaToolsApp::RegisterServerComponents()
{
    // Register any custom objects, services, etc. here
}

// OMF entry point — DllMain is provided by mfc140(u/ud).lib (UseOfMfc=Dynamic)
IMPLEMENT_ARX_ENTRYPOINT(CArqaToolsApp)


// RELOAD command - Unload and reload the plugin
void CArqaToolsApp::reloadCommand()
{
    acutPrintf(_T("\n=== Reload ArqaTools Plugin ===\n"));
    
    // Get the path to the currently loaded ARX file
    TCHAR arxPath[MAX_PATH];
    HMODULE hModule = nullptr;
    
    // Get the module handle for this DLL
    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCTSTR)&CArqaToolsApp::reloadCommand, &hModule))
    {
        if (GetModuleFileName(hModule, arxPath, MAX_PATH) > 0)
        {
            // LISP strings treat '\' as an escape character, so the pasteable
            // (arxload ...) form needs forward slashes.
            CString lispPath(arxPath);
            lispPath.Replace(_T('\\'), _T('/'));

            acutPrintf(_T("Plugin path: %s\n\n"), arxPath);
            acutPrintf(_T("NOTE: Cannot unload ARX while command is running.\n"));
            acutPrintf(_T("Copy and paste this LISP command after ATRELOAD completes:\n\n"));
            acutPrintf(_T("(progn (arxunload \"ArqaTools.arx\") (arxload \"%s\"))\n\n"), (LPCTSTR)lispPath);
            acutPrintf(_T("Or: ARX > Unload > ArqaTools.arx, then APPLOAD and browse to the path above.\n"));
            acutPrintf(_T("(RELOADHW from ReloadArqaTools.lsp does both in one step.)\n"));
        }
        else
        {
            acutPrintf(_T("ERROR: Could not get module file name.\n"));
        }
    }
    else
    {
        acutPrintf(_T("ERROR: Could not get module handle.\n"));
    }
}

// VERSION command - Display version and build information
void CArqaToolsApp::versionCommand()
{
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("  ArqaTools AutoCAD 2025 Plugin\n"));
    acutPrintf(_T("====================================\n"));
    acutPrintf(_T("%s\n"), GetVersionString());
    
    // Get module path
    HMODULE hModule = nullptr;
    TCHAR modulePath[MAX_PATH] = {0};
    
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCTSTR)&versionCommand, &hModule);
    
    if (hModule)
    {
        GetModuleFileName(hModule, modulePath, MAX_PATH);
        
        // Get file timestamp
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesEx(modulePath, GetFileExInfoStandard, &fileInfo))
        {
            FILETIME localFileTime;
            SYSTEMTIME sysTime;
            FileTimeToLocalFileTime(&fileInfo.ftLastWriteTime, &localFileTime);
            FileTimeToSystemTime(&localFileTime, &sysTime);
            
            acutPrintf(_T("File Date: %04d-%02d-%02d %02d:%02d:%02d\n"),
                      sysTime.wYear, sysTime.wMonth, sysTime.wDay,
                      sysTime.wHour, sysTime.wMinute, sysTime.wSecond);
        }
        
        acutPrintf(_T("Location: %s\n"), modulePath);
    }
    
    acutPrintf(_T("\nCompiler: Visual Studio 2022 (v143)\n"));
    acutPrintf(_T("AutoCAD: 2025 ObjectARX\n"));
    acutPrintf(_T("====================================\n"));
}

// ATHELP command - Display all available commands
void CArqaToolsApp::arqaHelpCommand()
{
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("  ArqaTools Plugin Commands\n"));
    acutPrintf(_T("====================================\n"));
    
    acutPrintf(_T("\n--- DRAWING COMMANDS ---\n"));
    
    acutPrintf(_T("\n--- POLYLINE BOOLEAN OPERATIONS ---\n"));
    acutPrintf(_T("ATBOOLPOLY  - Boolean operations menu (union/subtract/intersect) (Lua command)\n"));
    acutPrintf(_T("ATUNIONPOLY - Union of two polylines (Lua command)\n"));
    acutPrintf(_T("ATSUBPOLY   - Subtract second polyline from first (Lua command)\n"));
    acutPrintf(_T("ATINPOLY    - Intersection of two polylines (Lua command)\n"));
    acutPrintf(_T("ATREG2POLY  - Convert region to polyline (Lua command)\n"));
    
    acutPrintf(_T("\n--- ALIGNMENT COMMANDS ---\n"));
    acutPrintf(_T("ATALX       - Align objects by X coordinate (Lua command)\n"));
    acutPrintf(_T("ATALY       - Align objects by Y coordinate (Lua command)\n"));
    acutPrintf(_T("ATALZ       - Align objects by Z coordinate (Lua command)\n"));
    acutPrintf(_T("ATPLACEMID  - Place object at midpoint between two points (Lua command)\n"));
    
    acutPrintf(_T("\n--- RESTRICTED MOVEMENT ---\n"));
    acutPrintf(_T("ATMX        - Move objects in X direction only (Lua command)\n"));
    acutPrintf(_T("ATMY        - Move objects in Y direction only (Lua command)\n"));
    acutPrintf(_T("ATMZ        - Move objects in Z direction only (Lua command)\n"));
    
    acutPrintf(_T("\n--- RESTRICTED COPY ---\n"));
    acutPrintf(_T("ATCX        - Copy objects in X direction only (Lua command)\n"));
    acutPrintf(_T("ATCY        - Copy objects in Y direction only (Lua command)\n"));
    acutPrintf(_T("ATCZ        - Copy objects in Z direction only (Lua command)\n"));
    
    acutPrintf(_T("\n--- LAYER TOOLS ---\n"));
    acutPrintf(_T("ATNL        - Quick new layer (create and set as current) (Lua command)\n"));
    acutPrintf(_T("ATCHGTOLAYER- Change selected objects to current layer (Lua command)\n"));
    acutPrintf(_T("ATMATCHLAYER- Change objects to the layer of a source object (Lua command)\n"));
    acutPrintf(_T("ATFREEZELAYER- Freeze layer by selecting an object on it (Lua command)\n"));
    
    acutPrintf(_T("\n--- DISTRIBUTION & NUMBERING ---\n"));
    acutPrintf(_T("ATDISTLINE      - Distribute objects evenly along a line (Lua command)\n"));
    acutPrintf(_T("ATDISTBETWEEN   - Distribute objects between two points, excludes endpoints (Lua command)\n"));
    acutPrintf(_T("ATDISTEQUAL     - Distribute with equal spacing, half-space at ends (Lua command)\n"));
    acutPrintf(_T("ATDISTCOPYLINE  - Copy one object N times along a line, endpoints included (Lua command)\n"));
    acutPrintf(_T("ATDISTCOPYBETWEEN - Copy one object N times between two points (Lua command)\n"));
    acutPrintf(_T("ATDISTCOPYEQUAL - Copy one object N times with equal spacing (Lua command)\n"));
    acutPrintf(_T("ATDISTTOLINE    - Copy N objects distributed along a picked line or curve (Lua command)\n"));
    acutPrintf(_T("ATSEQNUM        - Place sequential numbers at picked points (Lua command)\n"));
    
    acutPrintf(_T("\n--- TEXT MANIPULATION ---\n"));
    acutPrintf(_T("ATCOPYTEXT   - Copy text content from one text to others (Lua command)\n"));
    acutPrintf(_T("ATCOPYSTYLE  - Copy text style properties (not height) (Lua command)\n"));
    acutPrintf(_T("ATCOPYTEXTFULL - Copy text style AND dimensions (height) (Lua command)\n"));
    acutPrintf(_T("ATCOPYDIMSTYLE - Copy dimension style from one dim to others (Lua command)\n"));
    acutPrintf(_T("ATSUMTEXT    - Sum numeric values from selected text objects (Lua command)\n"));
    acutPrintf(_T("ATSCALETEXT  - Scale text height of selected objects by a factor (Lua command)\n"));
    
    acutPrintf(_T("\n--- AREA TOOLS ---\n"));
    acutPrintf(_T("ATINSERTAREA  - Insert auto-updating area text in closed polyline (Lua command)\n"));
    acutPrintf(_T("ATPERIMETER   - Insert perimeter text in closed polyline (Lua command)\n"));
    acutPrintf(_T("ATSUMLENGTH   - Insert auto-updating sum of lengths (polylines/arcs/circles/lines) (Lua command)\n"));
    acutPrintf(_T("ATLINEARLENGTH - Insert length text on a line or open polyline (Lua command)\n"));
    acutPrintf(_T("ATTAGALL      - Insert length text on all selected lines/polylines (Lua command)\n"));
    acutPrintf(_T("ATSPLITLINE   - Split a line by intersecting lines, creating individual segments (Lua command)\n"));
    acutPrintf(_T("ATSPLITPOLI   - Split a polyline by intersecting lines, creating individual polyline segments (Lua command)\n"));
    acutPrintf(_T("ATROOMTAG     - Insert room name + area label in closed polyline (Lua command)\n"));
    acutPrintf(_T("ATCOUNTBLOCKS - Count block instances in selection or drawing (Lua command)\n"));
    
    acutPrintf(_T("\n--- DECORATIVE / PATTERN TOOLS ---\n"));
    acutPrintf(_T("ATARABESQUE       - Draw geometric arabesque patterns (rosette/star/petals) (Lua command)\n"));
    acutPrintf(_T("ATARABESCORL      - Retícula de arabesco andaluz 30/45  (param: A) (Lua command)\n"));
    acutPrintf(_T("ATARABESCOTOROSOL - Arabesco nazari 3D solido sobre toro (3DFACE renderable) (Lua command)\n"));
    acutPrintf(_T("ATARABESCOHIPSOL  - Arabesco nazari 3D solido sobre paraboloide hiperbolico (Lua command)\n"));
    acutPrintf(_T("ATHOJANAZARI      - Patron de hoja nazari hexagonal (La Alhambra) (Lua command)\n"));
    
    acutPrintf(_T("\n--- GOLDEN RECTANGLE ---\n"));
    acutPrintf(_T("ATGOLDENRECT    - Draw golden-ratio rectangle spiral (Lua command)\n"));
    acutPrintf(_T("ATGOLDENRECTIN  - Place golden rectangles inside a container (Lua command)\n"));
    acutPrintf(_T("ATGOLDENRECTINW - Place custom-proportion rectangles inside a container (Lua command)\n"));
    
    acutPrintf(_T("\n--- AI ASSISTANT ---\n"));
    acutPrintf(_T("ATAIASK       - Ask GitHub Copilot a question\n"));
    acutPrintf(_T("ATAIDRAW      - Draw using natural language (suggests commands)\n"));
    acutPrintf(_T("ATAILISP      - Generate and execute AutoLISP code from description\n"));
    acutPrintf(_T("ATAIFIX       - Report error and get corrected code\n"));
    acutPrintf(_T("ATAIHELP      - Show AI knowledge base of custom commands\n"));
    acutPrintf(_T("ATAICLEAR     - Clear conversation history (start fresh)\n"));
    acutPrintf(_T("ATAICONFIG    - Open ai_config.lua (providers, models, vision, limits)\n"));
    acutPrintf(_T("ATAISETENDPOINT - Choose the active provider from ai_config.lua\n"));
    acutPrintf(_T("ATAISETMODEL  - Set the active provider's model in ai_config.lua\n"));
    acutPrintf(_T("ATAISETTOKEN  - Store the API key for the active provider (registry)\n"));
    acutPrintf(_T("ATAITEST      - Test API connection\n"));
    acutPrintf(_T("ATAILISTMODELS - List the active provider's models (models_url)\n"));
    
    acutPrintf(_T("\n--- ACML INTERPRETER ---\n"));
    acutPrintf(_T("ATACML      - Run ACML script\n"));
    acutPrintf(_T("ATACMLCHECK - Validate ACML script\n"));
    acutPrintf(_T("ATACMLLEX   - Lex ACML script\n"));
    
    acutPrintf(_T("\n--- DATABASE ---\n"));
    acutPrintf(_T("ATCATENTITIES - Categorize entities by type (Lua command)\n"));

    acutPrintf(_T("\n--- EXPORT ---\n"));
    acutPrintf(_T("ATSVGEXPORT - Export selected entities (incl. block references, e.g. wall/door/window blocks) to an .svg file in Documents (Lua command)\n"));

    acutPrintf(_T("\n--- LUA SCRIPTING ---\n"));
    acutPrintf(_T("ATLUA       - Run a Lua script (inline or @path\\to\\file.lua) against the \"at\" API\n"));
    acutPrintf(_T("ATAILUA     - Ask the AI to write and run a Lua script for a natural-language request\n"));
    acutPrintf(_T("ATAICMD     - Ask the AI to create or change a Lua command (saved, reloaded live)\n"));
    acutPrintf(_T("ATLUACMDS   - List Lua commands      ATLUARELOAD - Reload the command files\n"));
    acutPrintf(_T("ATLUACMDDEL - Remove a Lua command   ATLUAFOLDER - Open the command files folder\n"));

    acutPrintf(_T("\n--- MCP (AI agents such as Claude Code, via ArqaToolsMcp.exe) ---\n"));
    acutPrintf(_T("ATMCPSTART  - Let MCP clients connect   ATMCPSTOP - Disconnect them\n"));
    acutPrintf(_T("ATMCPSTATUS - Show the bridge pipe name, connection and request count\n"));

    acutPrintf(_T("\n--- PLUGIN MANAGEMENT ---\n"));
    acutPrintf(_T("ATVERSION   - Display plugin version and build info\n"));
    acutPrintf(_T("ATHELP      - Display this help (all commands)\n"));
    acutPrintf(_T("ATRELOAD    - Show reload instructions\n"));
    
    acutPrintf(_T("\n--- LISP COMMANDS (from ReloadArqaTools.lsp) ---\n"));
    acutPrintf(_T("RELOADHW      - Unload and reload plugin\n"));
    acutPrintf(_T("RELOADHWBUILD - Rebuild and reload plugin\n"));
    acutPrintf(_T("UNLOADHW      - Unload plugin only\n"));
    acutPrintf(_T("RELOADHWPATH  - Reload from custom path\n"));
    
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("Type ATVERSION for build information\n"));
    acutPrintf(_T("====================================\n"));
}
