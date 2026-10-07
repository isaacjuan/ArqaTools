// ArqaTools.h - Header for ArqaTools AutoCAD plugin using OMF

#pragma once

// ============================================================================
// VERSION INFORMATION
// ============================================================================
#define ARQATOOLS_VERSION_MAJOR 1
#define ARQATOOLS_VERSION_MINOR 0
#define ARQATOOLS_BUILD_DATE __DATE__
#define ARQATOOLS_BUILD_TIME __TIME__

// Helper to get full version string
const TCHAR* GetVersionString();

// Forward declarations
class AcDbBlockTableRecord;

// OMF Module class declaration
class CArqaToolsApp : public AcRxArxApp
{
public:
    CArqaToolsApp();

    virtual AcRx::AppRetCode On_kInitAppMsg(void* pAppData) override;
    virtual AcRx::AppRetCode On_kUnloadAppMsg(void* pAppData) override;
    virtual void RegisterServerComponents() override;

    static void reloadCommand();
    static void versionCommand();
    static void arqaHelpCommand();
};
