// ArqaTools.cpp - Modern AutoCAD 2025 ObjectARX Plugin using OMF
// Demonstrates ObjectARX Module Framework (OMF) with AcRxArxApp
//
// Commands:
//   HELLO    - Creates a red circle with cross at specified point
//   DRAWBOX  - Creates a 3D wireframe box (blue)
//   BOOLPOLY - Boolean operations on polylines (union/intersection/subtract)

#include "StdAfx.h"
#include "ArqaTools.h"

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
#include "dbregion.h"
#include "dbgroup.h"

using namespace DistributeTools;
using namespace AlignTools;
using namespace AITools;
using namespace LayerTools;
using namespace TextTools;
using namespace ArabesqueTools;

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
        { _T("HELLO"),            helloWorldCommand           },
        { _T("DRAWBOX"),          drawBoxCommand              },
        { _T("ARQAHELP"),           arqaHelpCommand               },
        { _T("ARQAVERSION"),        versionCommand              },
        { _T("RELOAD"),           reloadCommand               },
        // Polyline / Boolean
        { _T("BOOLPOLY"),         booleanPolyCommand          },
        { _T("SUBPOLY"),          subtractPolyCommand         },
        { _T("INPOLY"),           intersectPolyCommand        },
        { _T("UNIONPOLY"),        unionPolyCommand            },
        { _T("REG2POLY"),         regionToPolyCommand         },
        // Align / Move / Copy
        { _T("ALX"),              alignXCommand               },
        { _T("ALY"),              alignYCommand               },
        { _T("ALZ"),              alignZCommand               },
        { _T("MX"),               moveXCommand                },
        { _T("MY"),               moveYCommand                },
        { _T("MZ"),               moveZCommand                },
        { _T("CX"),               copyXCommand                },
        { _T("CY"),               copyYCommand                },
        { _T("CZ"),               copyZCommand                },
        { _T("PLACEMID"),         placeMidCommand             },
        // Distribute
        { _T("DISTLINE"),         distributeLinearCommand     },
        { _T("DISTBETWEEN"),      distributeBetweenCommand    },
        { _T("DISTEQUAL"),        distributeEqualCommand      },
        { _T("DISTCOPYLINE"),     distributeCopyLinearCommand },
        { _T("DISTCOPYBETWEEN"),  distributeCopyBetweenCommand},
        { _T("DISTCOPYEQUAL"),    distributeCopyEqualCommand  },
        { _T("DISTTOLINE"),       alignToLineCommand          },
        // Sequence / Text
        { _T("SEQNUM"),           sequenceNumberCommand       },
        { _T("COPYTEXT"),         copyTextCommand             },
        { _T("COPYSTYLE"),        copyStyleCommand            },
        { _T("COPYTEXTFULL"),     copyTextFullCommand         },
        { _T("COPYDIMSTYLE"),     copyDimStyleCommand         },
        { _T("SUMTEXT"),          sumTextCommand              },
        { _T("SCALETEXT"),        scaleTextCommand            },
        // Area / Measurement
        { _T("INSERTAREA"),       insertAreaCommand           },
        { _T("SUMLENGTH"),        sumLengthCommand            },
        { _T("ROOMTAG"),          roomTagCommand              },
        { _T("PERIMETER"),        perimeterCommand            },
        { _T("LINEARLENGTH"),     linearLengthCommand         },
        { _T("COUNTBLOCKS"),      countBlocksCommand          },
        { _T("SPLITLINE"),        splitLineCommand            },
        { _T("SPLITPOLI"),        splitPoliCommand            },
        { _T("TAGALL"),           tagAllCommand               },
        // Layers
        { _T("CHGTOLAYER"),       changeToCurrentLayerCommand },
        { _T("NL"),               newLayerCommand             },
        { _T("MATCHLAYER"),       matchLayerCommand           },
        { _T("FREEZELAYER"),      freezeLayerCommand          },
        // AI
        { _T("AIASK"),            aiAskCommand                },
        { _T("AISETTOKEN"),       aiSetTokenCommand           },
        { _T("AISETENDPOINT"),    aiSetEndpointCommand        },
        { _T("AISETMODEL"),       aiSetModelCommand           },
        { _T("AITEST"),           aiTestCommand               },
        { _T("AILISTMODELS"),     aiListModelsCommand         },
        { _T("AIDRAW"),           aiDrawCommand               },
        { _T("AIHELP"),           aiHelpCommand               },
        { _T("AILISP"),           aiLispCommand               },
        { _T("AIFIX"),            aiFixCommand                },
        { _T("AICLEAR"),          aiClearHistoryCommand       },
        // Arabesque
        { _T("ARABESQUE"),        arabesqueCommand                },
        { _T("HOJANAZARI"),       hojaNazariCommand               },
        { _T("ARABESCORL"),       arabescoRlCommand               },
        { _T("ARABESCOTOROSOL"),  arabescotoroSolCommand          },
        { _T("ARABESCOHIPSOL"),   arabescohipSolCommand           },
        // ACML interpreter
        { _T("ACML"),             AcmlTools::acmlRunCommand       },
        { _T("ACMLCHECK"),        AcmlTools::acmlCheckCommand     },
        { _T("ACMLLEX"),          AcmlTools::acmlLexCommand       },
    };

    for (const auto& cmd : kCommands)
        acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), cmd.name, cmd.name, ACRX_CMD_MODAL, cmd.func);

#ifdef _DEBUG
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("SUPPRESSASSERTS"),   _T("SUPPRESSASSERTS"),   ACRX_CMD_MODAL, suppressAssertsCommand);
    acedRegCmds->addCommand(_T("ARQATOOLS_COMMANDS"), _T("UNSUPPRESSASSERTS"), _T("UNSUPPRESSASSERTS"), ACRX_CMD_MODAL, unsuppressAssertsCommand);
#endif

    InitAreaToolsPersistence();

    acutPrintf(_T("\n=== ArqaTools 2025 Plugin Loaded ===\n"));
    acutPrintf(_T("%s\n"), GetVersionString());
    acutPrintf(_T("Type ARQAHELP to see all available commands\n"));
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

// OMF entry point
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

// ARQAHELP command - Display all available commands
void CArqaToolsApp::arqaHelpCommand()
{
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("  ArqaTools Plugin Commands\n"));
    acutPrintf(_T("====================================\n"));
    
    acutPrintf(_T("\n--- DRAWING COMMANDS ---\n"));
    acutPrintf(_T("HELLO       - Create red circle with cross\n"));
    acutPrintf(_T("DRAWBOX     - Create 3D wireframe box\n"));
    
    acutPrintf(_T("\n--- POLYLINE BOOLEAN OPERATIONS ---\n"));
    acutPrintf(_T("BOOLPOLY    - Boolean operations menu (union/subtract/intersect)\n"));
    acutPrintf(_T("UNIONPOLY   - Union of two polylines\n"));
    acutPrintf(_T("SUBPOLY     - Subtract second polyline from first\n"));
    acutPrintf(_T("INPOLY      - Intersection of two polylines\n"));
    acutPrintf(_T("REG2POLY    - Convert region to polyline\n"));
    
    acutPrintf(_T("\n--- ALIGNMENT COMMANDS ---\n"));
    acutPrintf(_T("ALX         - Align objects by X coordinate\n"));
    acutPrintf(_T("ALY         - Align objects by Y coordinate\n"));
    acutPrintf(_T("ALZ         - Align objects by Z coordinate\n"));
    acutPrintf(_T("PLACEMID    - Place object at midpoint between two points\n"));
    
    acutPrintf(_T("\n--- RESTRICTED MOVEMENT ---\n"));
    acutPrintf(_T("MX          - Move objects in X direction only\n"));
    acutPrintf(_T("MY          - Move objects in Y direction only\n"));
    acutPrintf(_T("MZ          - Move objects in Z direction only\n"));
    
    acutPrintf(_T("\n--- RESTRICTED COPY ---\n"));
    acutPrintf(_T("CX          - Copy objects in X direction only\n"));
    acutPrintf(_T("CY          - Copy objects in Y direction only\n"));
    acutPrintf(_T("CZ          - Copy objects in Z direction only\n"));
    
    acutPrintf(_T("\n--- LAYER TOOLS ---\n"));
    acutPrintf(_T("NL          - Quick new layer (create and set as current)\n"));
    acutPrintf(_T("CHGTOLAYER  - Change selected objects to current layer\n"));
    acutPrintf(_T("MATCHLAYER  - Change objects to the layer of a source object\n"));
    acutPrintf(_T("FREEZELAYER - Freeze layer by selecting an object on it\n"));
    
    acutPrintf(_T("\n--- DISTRIBUTION & NUMBERING ---\n"));
    acutPrintf(_T("DISTLINE       - Distribute objects evenly along a line\n"));
    acutPrintf(_T("DISTBETWEEN    - Distribute objects between two points (excludes endpoints)\n"));
    acutPrintf(_T("DISTEQUAL      - Distribute with equal spacing (half-space at ends)\n"));
    acutPrintf(_T("DISTCOPYLINE   - Copy one object N times along a line (endpoints included)\n"));
    acutPrintf(_T("DISTCOPYBETWEEN- Copy one object N times between two points\n"));
    acutPrintf(_T("DISTCOPYEQUAL  - Copy one object N times with equal spacing\n"));
    acutPrintf(_T("DISTTOLINE     - Copy N objects distributed along a picked line entity\n"));
    acutPrintf(_T("SEQNUM         - Add sequential numbers to selected objects\n"));
    
    acutPrintf(_T("\n--- TEXT MANIPULATION ---\n"));
    acutPrintf(_T("COPYTEXT    - Copy text content from one text to others\n"));
    acutPrintf(_T("COPYSTYLE   - Copy text style properties (not height)\n"));
    acutPrintf(_T("COPYTEXTFULL- Copy text style AND dimensions (height)\n"));
    acutPrintf(_T("COPYDIMSTYLE- Copy dimension style from one dim to others\n"));
    acutPrintf(_T("SUMTEXT     - Sum numeric values from selected text objects\n"));
    acutPrintf(_T("SCALETEXT   - Scale text height of selected objects by a factor\n"));
    
    acutPrintf(_T("\n--- AREA TOOLS ---\n"));
    acutPrintf(_T("INSERTAREA  - Insert auto-updating area text in closed polyline\n"));
    acutPrintf(_T("PERIMETER   - Insert perimeter text in closed polyline\n"));
    acutPrintf(_T("SUMLENGTH   - Insert auto-updating sum of lengths (polylines/arcs/circles/lines)\n"));
    acutPrintf(_T("LINEARLENGTH- Insert length text on a line or open polyline\n"));
    acutPrintf(_T("TAGALL      - Insert length text on all selected lines/polylines\n"));
    acutPrintf(_T("SPLITLINE   - Split a line by intersecting lines, creating individual segments\n"));
    acutPrintf(_T("SPLITPOLI   - Split a polyline by intersecting lines, creating individual polyline segments\n"));
    acutPrintf(_T("ROOMTAG     - Insert room name + area label in closed polyline\n"));
    acutPrintf(_T("COUNTBLOCKS - Count block instances in selection or drawing\n"));
    
    acutPrintf(_T("\n--- DECORATIVE / PATTERN TOOLS ---\n"));
    acutPrintf(_T("ARABESQUE        - Draw geometric arabesque patterns (rosette/star/petals)\n"));
    acutPrintf(_T("ARABESCORL       - Retícula de arabesco andaluz 30/45  (param: A)\n"));
    acutPrintf(_T("ARABESCOTOROSOL  - Arabesco nazari 3D solido sobre toro (3DFACE renderable)\n"));
    acutPrintf(_T("ARABESCOHIPSOL   - Arabesco nazari 3D solido sobre paraboloide hiperbolico\n"));
    acutPrintf(_T("HOJANAZARI       - Patron de hoja nazari hexagonal (La Alhambra)\n"));
    
    acutPrintf(_T("\n--- AI ASSISTANT ---\n"));
    acutPrintf(_T("AIASK       - Ask GitHub Copilot a question\n"));
    acutPrintf(_T("AIDRAW      - Draw using natural language (suggests commands)\n"));
    acutPrintf(_T("AILISP      - Generate and execute AutoLISP code from description\n"));
    acutPrintf(_T("AIFIX       - Report error and get corrected code\n"));
    acutPrintf(_T("AIHELP      - Show AI knowledge base of custom commands\n"));
    acutPrintf(_T("AICLEAR     - Clear conversation history (start fresh)\n"));
    acutPrintf(_T("AISETTOKEN  - Set your GitHub API token\n"));
    acutPrintf(_T("AISETENDPOINT - Set API endpoint (Free/Subscription/Custom)\n"));
    acutPrintf(_T("AITEST      - Test API connection\n"));
    acutPrintf(_T("AILISTMODELS- List available models (Gemini only)\n"));
    
    acutPrintf(_T("\n--- PLUGIN MANAGEMENT ---\n"));
    acutPrintf(_T("ARQAVERSION   - Display plugin version and build info\n"));
    acutPrintf(_T("ARQAHELP      - Display this help (all commands)\n"));
    acutPrintf(_T("RELOAD      - Show reload instructions\n"));
    
    acutPrintf(_T("\n--- LISP COMMANDS (from ReloadArqaTools.lsp) ---\n"));
    acutPrintf(_T("RELOADHW      - Unload and reload plugin\n"));
    acutPrintf(_T("RELOADHWBUILD - Rebuild and reload plugin\n"));
    acutPrintf(_T("UNLOADHW      - Unload plugin only\n"));
    acutPrintf(_T("RELOADHWPATH  - Reload from custom path\n"));
    
    acutPrintf(_T("\n====================================\n"));
    acutPrintf(_T("Type ARQAVERSION for build information\n"));
    acutPrintf(_T("====================================\n"));
}
