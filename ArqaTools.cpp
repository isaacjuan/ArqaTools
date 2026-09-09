// ArqaTools.cpp - Modern AutoCAD 2025 ObjectARX Plugin using OMF
// Demonstrates ObjectARX Module Framework (OMF) with AcRxArxApp
//
// Commands:
//   HELLO    - Creates a red circle with cross at specified point
//   DRAWBOX  - Creates a 3D wireframe box (blue)
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
#include "PolylineTools.h"
#include "AlignTools.h"
#include "SeqNumTools.h"
#include "DistributeTools.h"
#include "TextTools.h"
#include "AreaTools.h"
#include "AITools.h"
#include "LayerTools.h"
#include "ArabesqueTools.h"
#include "AcmlTools.h"
#include "CategorizeTools.h"
#include "GoldenRectTools.h"
#include "dbregion.h"
#include "dbgroup.h"

using namespace DistributeTools;
using namespace AlignTools;
using namespace AITools;
using namespace LayerTools;
using namespace TextTools;
using namespace ArabesqueTools;
using namespace GoldenRectTools;

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
        { _T("ATHELLO"),            helloWorldCommand           },
        { _T("ATDRAWBOX"),          drawBoxCommand              },
        { _T("ATHELP"),             arqaHelpCommand             },
        { _T("ATVERSION"),          versionCommand              },
        { _T("ATRELOAD"),           reloadCommand               },
        // Polyline / Boolean
        { _T("ATBOOLPOLY"),         booleanPolyCommand          },
        { _T("ATSUBPOLY"),          subtractPolyCommand         },
        { _T("ATINPOLY"),           intersectPolyCommand        },
        { _T("ATUNIONPOLY"),        unionPolyCommand            },
        { _T("ATREG2POLY"),         regionToPolyCommand         },
        // Align / Move / Copy
        { _T("ATALX"),              alignXCommand               },
        { _T("ATALY"),              alignYCommand               },
        { _T("ATALZ"),              alignZCommand               },
        { _T("ATMX"),               moveXCommand                },
        { _T("ATMY"),               moveYCommand                },
        { _T("ATMZ"),               moveZCommand                },
        { _T("ATCX"),               copyXCommand                },
        { _T("ATCY"),               copyYCommand                },
        { _T("ATCZ"),               copyZCommand                },
        { _T("ATPLACEMID"),         placeMidCommand             },
        // Distribute
        { _T("ATDISTLINE"),         distributeLinearCommand     },
        { _T("ATDISTBETWEEN"),      distributeBetweenCommand    },
        { _T("ATDISTEQUAL"),        distributeEqualCommand      },
        { _T("ATDISTCOPYLINE"),     distributeCopyLinearCommand },
        { _T("ATDISTCOPYBETWEEN"),  distributeCopyBetweenCommand},
        { _T("ATDISTCOPYEQUAL"),    distributeCopyEqualCommand  },
        { _T("ATDISTTOLINE"),       alignToLineCommand          },
        // Sequence / Text
        { _T("ATSEQNUM"),           sequenceNumberCommand       },
        { _T("ATCOPYTEXT"),         copyTextCommand             },
        { _T("ATCOPYSTYLE"),        copyStyleCommand            },
        { _T("ATCOPYTEXTFULL"),     copyTextFullCommand         },
        { _T("ATCOPYDIMSTYLE"),     copyDimStyleCommand         },
        { _T("ATSUMTEXT"),          sumTextCommand              },
        { _T("ATSCALETEXT"),        scaleTextCommand            },
        // Area / Measurement
        { _T("ATINSERTAREA"),       insertAreaCommand           },
        { _T("ATSUMLENGTH"),        sumLengthCommand            },
        { _T("ATROOMTAG"),          roomTagCommand              },
        { _T("ATPERIMETER"),        perimeterCommand            },
        { _T("ATLINEARLENGTH"),     linearLengthCommand         },
        { _T("ATCOUNTBLOCKS"),      countBlocksCommand          },
        { _T("ATSPLITLINE"),        splitLineCommand            },
        { _T("ATSPLITPOLI"),        splitPoliCommand            },
        { _T("ATTAGALL"),           tagAllCommand               },
        // Layers
        { _T("ATCHGTOLAYER"),       changeToCurrentLayerCommand },
        { _T("ATNL"),               newLayerCommand             },
        { _T("ATMATCHLAYER"),       matchLayerCommand           },
        { _T("ATFREEZELAYER"),      freezeLayerCommand          },
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
        // Arabesque
        { _T("ATARABESQUE"),        arabesqueCommand            },
        { _T("ATHOJANAZARI"),       hojaNazariCommand           },
        { _T("ATARABESCORL"),       arabescoRlCommand           },
        { _T("ATARABESCOTOROSOL"),  arabescotoroSolCommand      },
        { _T("ATARABESCOHIPSOL"),   arabescohipSolCommand       },
        // Golden rect
        { _T("ATGOLDENRECT"),       goldenRectCommand           },
        { _T("ATGOLDENRECTIN"),     goldenRectInCommand         },
        { _T("ATGOLDENRECTINW"),    goldenRectInWCommand        },
        // ACML interpreter
        { _T("ATACML"),             AcmlTools::acmlRunCommand       },
        { _T("ATACMLCHECK"),        AcmlTools::acmlCheckCommand     },
        { _T("ATACMLLEX"),          AcmlTools::acmlLexCommand       },
        // Database
        { _T("ATCATENTITIES"),      CategorizeTools::catEntitiesCommand  },
    };

    for (const auto& cmd : kCommands)
        acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), cmd.name, cmd.name, ACRX_CMD_MODAL, cmd.func);

#ifdef _DEBUG
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("ATSUPPRESSASSERTS"),   _T("ATSUPPRESSASSERTS"),   ACRX_CMD_MODAL, suppressAssertsCommand);
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("ATUNSUPPRESSASSERTS"), _T("ATUNSUPPRESSASSERTS"), ACRX_CMD_MODAL, unsuppressAssertsCommand);
#endif

    InitAreaToolsPersistence();

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

// Helper function: Add entity to model space with color
static Acad::ErrorStatus AddEntityToModelSpace(AcDbEntity* pEntity, int colorIndex, AcDbBlockTableRecord* pModelSpace)
{
    if (!pEntity || !pModelSpace)
        return Acad::eNullPtr;
    
    pEntity->setColorIndex(colorIndex);
    Acad::ErrorStatus es = pModelSpace->appendAcDbEntity(pEntity);
    pEntity->close();
    
    return es;
}

// Helper function: Create a line entity
static AcDbLine* CreateLine(const AcGePoint3d& start, const AcGePoint3d& end)
{
    return new AcDbLine(start, end);
}

// Helper function: Draw a cross (horizontal and vertical lines) at a point
static void DrawCross(const AcGePoint3d& center, double halfLength, int colorIndex, AcDbBlockTableRecord* pModelSpace)
{
    const AcGeVector3d offsetX(halfLength, 0.0, 0.0);
    const AcGeVector3d offsetY(0.0, halfLength, 0.0);
    
    // Horizontal line
    AcDbLine* pLineH = CreateLine(center - offsetX, center + offsetX);
    AddEntityToModelSpace(pLineH, colorIndex, pModelSpace);
    
    // Vertical line
    AcDbLine* pLineV = CreateLine(center - offsetY, center + offsetY);
    AddEntityToModelSpace(pLineV, colorIndex, pModelSpace);
}

// Helper function: Display welcome message
static void DisplayWelcomeMessage()
{
    acutPrintf(_T("\n==========================================\n"));
    acutPrintf(_T("   Hello World from AutoCAD 2025!      \n"));
    acutPrintf(_T("   Simple ObjectARX Plugin Example     \n"));
    acutPrintf(_T("==========================================\n"));
}

// Helper function: Draw circle with cross at specified point
static Acad::ErrorStatus DrawCircleWithCross(const AcGePoint3d& center, double radius, int colorIndex)
{
    // Get model space
    AcDbBlockTableRecord* pModelSpace = nullptr;
    Acad::ErrorStatus es = CommonTools::GetModelSpace(pModelSpace);
    if (es != Acad::eOk)
        return es;
    
    // Create and add circle
    AcDbCircle* pCircle = new AcDbCircle(center, AcGeVector3d::kZAxis, radius);
    AddEntityToModelSpace(pCircle, colorIndex, pModelSpace);
    
    // Draw cross at center
    const double crossHalfLength = 0.16 * radius;
    DrawCross(center, crossHalfLength, colorIndex, pModelSpace);
    
    pModelSpace->close();
    return Acad::eOk;
}

// Command implementation
void CArqaToolsApp::helloWorldCommand()
{
    DisplayWelcomeMessage();

    // Get a point from the user
    ads_point pt;
    int result = acedGetPoint(NULL, _T("\nPick a point (or press ESC): "), pt);
    
    if (result != RTNORM)
    {
        acutPrintf(_T("\nCommand cancelled.\n"));
        return;
    }

    acutPrintf(_T("\nYou picked point: X=%.2f, Y=%.2f, Z=%.2f\n"), 
               pt[X], pt[Y], pt[Z]);
    
    // Draw the circle with cross
    const AcGePoint3d center(pt[X], pt[Y], pt[Z]);
    const double radius = 100.0;
    const int redColor = 1;
    
    if (DrawCircleWithCross(center, radius, redColor) != Acad::eOk)
    {
        acutPrintf(_T("\nError: Could not create entities.\n"));
        return;
    }
    
    acutPrintf(_T("\nCircle with cross created at the selected point!\n"));
}

// Helper function: Draw a 3D wireframe box at specified point
static Acad::ErrorStatus DrawBox(const AcGePoint3d& corner, double width, double height, double depth, int colorIndex)
{
    // Get model space
    AcDbBlockTableRecord* pModelSpace = nullptr;
    Acad::ErrorStatus es = CommonTools::GetModelSpace(pModelSpace);
    if (es != Acad::eOk)
        return es;
    
    // Calculate the 8 corners of the box
    AcGePoint3d p1 = corner;                                        // Bottom front left
    AcGePoint3d p2(corner.x + width, corner.y, corner.z);         // Bottom front right
    AcGePoint3d p3(corner.x + width, corner.y + height, corner.z); // Bottom back right
    AcGePoint3d p4(corner.x, corner.y + height, corner.z);        // Bottom back left
    AcGePoint3d p5(corner.x, corner.y, corner.z + depth);         // Top front left
    AcGePoint3d p6(corner.x + width, corner.y, corner.z + depth); // Top front right
    AcGePoint3d p7(corner.x + width, corner.y + height, corner.z + depth); // Top back right
    AcGePoint3d p8(corner.x, corner.y + height, corner.z + depth); // Top back left
    
    // Create the 12 lines of the 3D wireframe box
    // Bottom face
    AcDbLine* lines[12];
    lines[0] = CreateLine(p1, p2);
    lines[1] = CreateLine(p2, p3);
    lines[2] = CreateLine(p3, p4);
    lines[3] = CreateLine(p4, p1);
    // Top face
    lines[4] = CreateLine(p5, p6);
    lines[5] = CreateLine(p6, p7);
    lines[6] = CreateLine(p7, p8);
    lines[7] = CreateLine(p8, p5);
    // Vertical lines
    lines[8] = CreateLine(p1, p5);
    lines[9] = CreateLine(p2, p6);
    lines[10] = CreateLine(p3, p7);
    lines[11] = CreateLine(p4, p8);
    
    // Add all lines to model space
    for (int i = 0; i < 12; i++)
    {
        AddEntityToModelSpace(lines[i], colorIndex, pModelSpace);
    }
    
    pModelSpace->close();
    return Acad::eOk;
}

// DRAWBOX command implementation
void CArqaToolsApp::drawBoxCommand()
{
    acutPrintf(_T("\n==========================================\n"));
    acutPrintf(_T("   Draw Box Command                     \n"));
    acutPrintf(_T("==========================================\n"));

    // Get first corner from the user
    ads_point pt;
    int result = acedGetPoint(NULL, _T("\nPick first corner (or press ESC): "), pt);
    
    if (result != RTNORM)
    {
        acutPrintf(_T("\nCommand cancelled.\n"));
        return;
    }

    acutPrintf(_T("\nFirst corner: X=%.2f, Y=%.2f, Z=%.2f\n"), 
               pt[X], pt[Y], pt[Z]);
    
    // Draw the 3D wireframe box
    const AcGePoint3d corner(pt[X], pt[Y], pt[Z]);
    const double width = 150.0;
    const double height = 100.0;
    const double depth = 75.0;
    const int blueColor = 5;
    
    if (DrawBox(corner, width, height, depth, blueColor) != Acad::eOk)
    {
        acutPrintf(_T("\nError: Could not create box.\n"));
        return;
    }
    
    acutPrintf(_T("\n3D wireframe box created! Width=%.2f, Height=%.2f, Depth=%.2f\n"), width, height, depth);
}

// BOOLPOLY command implementation
void CArqaToolsApp::booleanPolyCommand()
{
    PolylineTools::booleanPolyCommand();
}

// SUBPOLY command - Subtract second polyline from first
void CArqaToolsApp::subtractPolyCommand()
{
    PolylineTools::subtractPolyCommand();
}

// INPOLY command - Intersection of two polylines
void CArqaToolsApp::intersectPolyCommand()
{
    PolylineTools::intersectPolyCommand();
}

// UNIONPOLY command - Union of two polylines
void CArqaToolsApp::unionPolyCommand()
{
    PolylineTools::unionPolyCommand();
}

// REG2POLY command - Convert region to polyline
void CArqaToolsApp::regionToPolyCommand()
{
    PolylineTools::regionToPolyCommand();
}


// ALX command - Align to X coordinate
void CArqaToolsApp::alignXCommand()
{
    AlignTools::alignXCommand();
}

// ALY command - Align to Y coordinate
void CArqaToolsApp::alignYCommand()
{
    AlignTools::alignYCommand();
}

// ALZ command - Align to Z coordinate
void CArqaToolsApp::alignZCommand()
{
    AlignTools::alignZCommand();
}

// SEQNUM command - Create sequence of numbers at specified points
void CArqaToolsApp::sequenceNumberCommand()
{
    SeqNumTools::sequenceNumberCommand();
}

// DISTLINE command - Distribute objects evenly along a line
void CArqaToolsApp::distributeLinearCommand()
{
    DistributeTools::distributeLinearCommand();
}

void CArqaToolsApp::distributeCopyLinearCommand()
{
    DistributeTools::distributeCopyLinearCommand();
}

void CArqaToolsApp::distributeCopyBetweenCommand()
{
    DistributeTools::distributeCopyBetweenCommand();
}

void CArqaToolsApp::distributeCopyEqualCommand()
{
    DistributeTools::distributeCopyEqualCommand();
}

// COPYTEXT command - Copy text content from one object to others
void CArqaToolsApp::copyTextCommand()
{
    TextTools::copyTextCommand();
}

// COPYSTYLE command - Copy text style properties from one object to others
void CArqaToolsApp::copyStyleCommand()
{
    TextTools::copyStyleCommand();
}

// COPYTEXTFULL command - Copy text style AND dimensions (height)
void CArqaToolsApp::copyTextFullCommand()
{
    TextTools::copyTextFullCommand();
}

// COPYDIMSTYLE command - Copy dimension style from one dimension to others
void CArqaToolsApp::copyDimStyleCommand()
{
    TextTools::copyDimStyleCommand();
}

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
            acutPrintf(_T("Plugin path: %s\n\n"), arxPath);
            acutPrintf(_T("NOTE: Cannot unload ARX while command is running.\n"));
            acutPrintf(_T("Copy and paste this LISP command after RELOAD completes:\n\n"));
            acutPrintf(_T("(progn (arxunload \"ArqaTools.arx\") (arxload \"%s\"))\n\n"), arxPath);
            acutPrintf(_T("Or use these commands:\n"));
            acutPrintf(_T("  ARX UNLOAD ArqaTools.arx\n"));
            acutPrintf(_T("  NETLOAD %s\n"), arxPath);
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

// PLACEMID command - Move object to midpoint between two points
void CArqaToolsApp::placeMidCommand()
{
    AlignTools::placeMidCommand();
}

// ROOMTAG command
void CArqaToolsApp::roomTagCommand()
{
    ::roomTagCommand();
}

// PERIMETER command
void CArqaToolsApp::perimeterCommand()
{
    ::perimeterCommand();
}

// LINEARLENGTH command
void CArqaToolsApp::linearLengthCommand()
{
    ::linearLengthCommand();
}

// COUNTBLOCKS command
void CArqaToolsApp::countBlocksCommand()
{
    ::countBlocksCommand();
}

// SCALETEXT command
void CArqaToolsApp::scaleTextCommand()
{
    TextTools::scaleTextCommand();
}

// SPLITLINE command
void CArqaToolsApp::splitLineCommand()
{
    ::splitLineCommand();
}

// SPLITPOLI command
void CArqaToolsApp::splitPoliCommand()
{
    ::splitPoliCommand();
}

// TAGALL command
void CArqaToolsApp::tagAllCommand()
{
    ::tagAllCommand();
}

// DISTTOLINE command
void CArqaToolsApp::alignToLineCommand()
{
    DistributeTools::alignToLineCommand();
}

// ARABESQUE command
void CArqaToolsApp::arabesqueCommand()
{
    ArabesqueTools::arabesqueCommand();
}

// HOJANAZARI command
void CArqaToolsApp::hojaNazariCommand()
{
    ArabesqueTools::hojaNazariCommand();
}

// ARABESCORL command
void CArqaToolsApp::arabescoRlCommand()
{
    ArabesqueTools::arabescoRlCommand();
}

// ARABESCOTOROSOL command
void CArqaToolsApp::arabescotoroSolCommand()
{
    ArabesqueTools::arabescotoroSolCommand();
}

// ARABESCOHIPSOL command
void CArqaToolsApp::arabescohipSolCommand()
{
    ArabesqueTools::arabescohipSolCommand();
}

// MATCHLAYER command
void CArqaToolsApp::matchLayerCommand()
{
    LayerTools::matchLayerCommand();
}

// FREEZELAYER command
void CArqaToolsApp::freezeLayerCommand()
{
    LayerTools::freezeLayerCommand();
}

// ATGOLDENRECT command
void CArqaToolsApp::goldenRectCommand()
{
    GoldenRectTools::goldenRectCommand();
}

// ATGOLDENRECTIN command
void CArqaToolsApp::goldenRectInCommand()
{
    GoldenRectTools::goldenRectInCommand();
}

// ATGOLDENRECTINW command
void CArqaToolsApp::goldenRectInWCommand()
{
    GoldenRectTools::goldenRectInWCommand();
}

// ATHELP command - Display all available commands
void CArqaToolsApp::arqaHelpCommand()
{
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("  ArqaTools Plugin Commands\n"));
    acutPrintf(_T("====================================\n"));
    
    acutPrintf(_T("\n--- DRAWING COMMANDS ---\n"));
    acutPrintf(_T("ATHELLO     - Create red circle with cross\n"));
    acutPrintf(_T("ATDRAWBOX   - Create 3D wireframe box\n"));
    
    acutPrintf(_T("\n--- POLYLINE BOOLEAN OPERATIONS ---\n"));
    acutPrintf(_T("ATBOOLPOLY  - Boolean operations menu (union/subtract/intersect)\n"));
    acutPrintf(_T("ATUNIONPOLY - Union of two polylines\n"));
    acutPrintf(_T("ATSUBPOLY   - Subtract second polyline from first\n"));
    acutPrintf(_T("ATINPOLY    - Intersection of two polylines\n"));
    acutPrintf(_T("ATREG2POLY  - Convert region to polyline\n"));
    
    acutPrintf(_T("\n--- ALIGNMENT COMMANDS ---\n"));
    acutPrintf(_T("ATALX       - Align objects by X coordinate\n"));
    acutPrintf(_T("ATALY       - Align objects by Y coordinate\n"));
    acutPrintf(_T("ATALZ       - Align objects by Z coordinate\n"));
    acutPrintf(_T("ATPLACEMID  - Place object at midpoint between two points\n"));
    
    acutPrintf(_T("\n--- RESTRICTED MOVEMENT ---\n"));
    acutPrintf(_T("ATMX        - Move objects in X direction only\n"));
    acutPrintf(_T("ATMY        - Move objects in Y direction only\n"));
    acutPrintf(_T("ATMZ        - Move objects in Z direction only\n"));
    
    acutPrintf(_T("\n--- RESTRICTED COPY ---\n"));
    acutPrintf(_T("ATCX        - Copy objects in X direction only\n"));
    acutPrintf(_T("ATCY        - Copy objects in Y direction only\n"));
    acutPrintf(_T("ATCZ        - Copy objects in Z direction only\n"));
    
    acutPrintf(_T("\n--- LAYER TOOLS ---\n"));
    acutPrintf(_T("ATNL        - Quick new layer (create and set as current)\n"));
    acutPrintf(_T("ATCHGTOLAYER- Change selected objects to current layer\n"));
    acutPrintf(_T("ATMATCHLAYER- Change objects to the layer of a source object\n"));
    acutPrintf(_T("ATFREEZELAYER- Freeze layer by selecting an object on it\n"));
    
    acutPrintf(_T("\n--- DISTRIBUTION & NUMBERING ---\n"));
    acutPrintf(_T("ATDISTLINE      - Distribute objects evenly along a line\n"));
    acutPrintf(_T("ATDISTBETWEEN   - Distribute objects between two points (excludes endpoints)\n"));
    acutPrintf(_T("ATDISTEQUAL     - Distribute with equal spacing (half-space at ends)\n"));
    acutPrintf(_T("ATDISTCOPYLINE  - Copy one object N times along a line (endpoints included)\n"));
    acutPrintf(_T("ATDISTCOPYBETWEEN - Copy one object N times between two points\n"));
    acutPrintf(_T("ATDISTCOPYEQUAL - Copy one object N times with equal spacing\n"));
    acutPrintf(_T("ATDISTTOLINE    - Copy N objects distributed along a picked line entity\n"));
    acutPrintf(_T("ATSEQNUM        - Add sequential numbers to selected objects\n"));
    
    acutPrintf(_T("\n--- TEXT MANIPULATION ---\n"));
    acutPrintf(_T("ATCOPYTEXT   - Copy text content from one text to others\n"));
    acutPrintf(_T("ATCOPYSTYLE  - Copy text style properties (not height)\n"));
    acutPrintf(_T("ATCOPYTEXTFULL - Copy text style AND dimensions (height)\n"));
    acutPrintf(_T("ATCOPYDIMSTYLE - Copy dimension style from one dim to others\n"));
    acutPrintf(_T("ATSUMTEXT    - Sum numeric values from selected text objects\n"));
    acutPrintf(_T("ATSCALETEXT  - Scale text height of selected objects by a factor\n"));
    
    acutPrintf(_T("\n--- AREA TOOLS ---\n"));
    acutPrintf(_T("ATINSERTAREA  - Insert auto-updating area text in closed polyline\n"));
    acutPrintf(_T("ATPERIMETER   - Insert perimeter text in closed polyline\n"));
    acutPrintf(_T("ATSUMLENGTH   - Insert auto-updating sum of lengths (polylines/arcs/circles/lines)\n"));
    acutPrintf(_T("ATLINEARLENGTH - Insert length text on a line or open polyline\n"));
    acutPrintf(_T("ATTAGALL      - Insert length text on all selected lines/polylines\n"));
    acutPrintf(_T("ATSPLITLINE   - Split a line by intersecting lines, creating individual segments\n"));
    acutPrintf(_T("ATSPLITPOLI   - Split a polyline by intersecting lines, creating individual polyline segments\n"));
    acutPrintf(_T("ATROOMTAG     - Insert room name + area label in closed polyline\n"));
    acutPrintf(_T("ATCOUNTBLOCKS - Count block instances in selection or drawing\n"));
    
    acutPrintf(_T("\n--- DECORATIVE / PATTERN TOOLS ---\n"));
    acutPrintf(_T("ATARABESQUE       - Draw geometric arabesque patterns (rosette/star/petals)\n"));
    acutPrintf(_T("ATARABESCORL      - Retícula de arabesco andaluz 30/45  (param: A)\n"));
    acutPrintf(_T("ATARABESCOTOROSOL - Arabesco nazari 3D solido sobre toro (3DFACE renderable)\n"));
    acutPrintf(_T("ATARABESCOHIPSOL  - Arabesco nazari 3D solido sobre paraboloide hiperbolico\n"));
    acutPrintf(_T("ATHOJANAZARI      - Patron de hoja nazari hexagonal (La Alhambra)\n"));
    
    acutPrintf(_T("\n--- GOLDEN RECTANGLE ---\n"));
    acutPrintf(_T("ATGOLDENRECT    - Draw golden-ratio rectangle spiral\n"));
    acutPrintf(_T("ATGOLDENRECTIN  - Place golden rectangles inside a container\n"));
    acutPrintf(_T("ATGOLDENRECTINW - Place custom-proportion rectangles inside a container\n"));
    
    acutPrintf(_T("\n--- AI ASSISTANT ---\n"));
    acutPrintf(_T("ATAIASK       - Ask GitHub Copilot a question\n"));
    acutPrintf(_T("ATAIDRAW      - Draw using natural language (suggests commands)\n"));
    acutPrintf(_T("ATAILISP      - Generate and execute AutoLISP code from description\n"));
    acutPrintf(_T("ATAIFIX       - Report error and get corrected code\n"));
    acutPrintf(_T("ATAIHELP      - Show AI knowledge base of custom commands\n"));
    acutPrintf(_T("ATAICLEAR     - Clear conversation history (start fresh)\n"));
    acutPrintf(_T("ATAISETTOKEN  - Set your GitHub API token\n"));
    acutPrintf(_T("ATAISETENDPOINT - Set API endpoint (Free/Subscription/Custom)\n"));
    acutPrintf(_T("ATAITEST      - Test API connection\n"));
    acutPrintf(_T("ATAILISTMODELS - List available models (Gemini only)\n"));
    
    acutPrintf(_T("\n--- ACML INTERPRETER ---\n"));
    acutPrintf(_T("ATACML      - Run ACML script\n"));
    acutPrintf(_T("ATACMLCHECK - Validate ACML script\n"));
    acutPrintf(_T("ATACMLLEX   - Lex ACML script\n"));
    
    acutPrintf(_T("\n--- DATABASE ---\n"));
    acutPrintf(_T("ATCATENTITIES - Categorize entities by type\n"));
    
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
