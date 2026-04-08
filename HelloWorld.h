// HelloWorld.h - Header for HelloWorld AutoCAD plugin using OMF

#pragma once

// ============================================================================
// VERSION INFORMATION
// ============================================================================
#define HELLOWORLD_VERSION_MAJOR 1
#define HELLOWORLD_VERSION_MINOR 0
#define HELLOWORLD_BUILD_DATE __DATE__
#define HELLOWORLD_BUILD_TIME __TIME__

// Helper to get full version string
const TCHAR* GetVersionString();

// Forward declarations
class AcDbBlockTableRecord;

// OMF Module class declaration
class CHelloWorldApp : public AcRxArxApp
{
public:
    CHelloWorldApp();

    virtual AcRx::AppRetCode On_kInitAppMsg(void* pAppData) override;
    virtual AcRx::AppRetCode On_kUnloadAppMsg(void* pAppData) override;
    virtual void RegisterServerComponents() override;

    static void helloWorldCommand();
    static void drawBoxCommand();
    static void booleanPolyCommand();
    static void subtractPolyCommand();
    static void intersectPolyCommand();
    static void unionPolyCommand();
    static void regionToPolyCommand();
    static void alignXCommand();
    static void alignYCommand();
    static void alignZCommand();
    static void sequenceNumberCommand();
    static void distributeLinearCommand();
    static void distributeCopyLinearCommand();
    static void distributeCopyBetweenCommand();
    static void distributeCopyEqualCommand();
    static void copyTextCommand();
    static void copyStyleCommand();
    static void copyTextFullCommand();
    static void copyDimStyleCommand();
    static void reloadCommand();
    static void versionCommand();
    static void hwHelpCommand();
    static void placeMidCommand();
    static void roomTagCommand();
    static void perimeterCommand();
    static void linearLengthCommand();
    static void countBlocksCommand();
    static void scaleTextCommand();
    static void matchLayerCommand();
    static void freezeLayerCommand();
    static void splitLineCommand();
    static void splitPoliCommand();
    static void tagAllCommand();
    static void alignToLineCommand();
    static void arabesqueCommand();
    static void hojaNazariCommand();
    static void arabescoRlCommand();
    static void arabescotoroSolCommand();
    static void arabescohipSolCommand();
};
