# HelloWorld AutoCAD 2025 Plugin - Modular Architecture

## Overview
The plugin has been refactored into a modular architecture, separating concerns into specialized modules for better code organization and maintainability.

## File Structure

### Core Files
- **HelloWorld.h** - Main plugin header with OMF class declarations and shared helper function exports
- **HelloWorld.cpp** - Main plugin implementation with OMF initialization, demo commands (HELLO, DRAWBOX)
- **StdAfx.h** - Precompiled headers with AutoCAD SDK includes and M_PI definition
- **StdAfx.cpp** - Precompiled header implementation

### Module Files

#### Polyline Tools Module
- **PolylineTools.h** - Header declaring polyline and boolean operation functions
- **PolylineTools.cpp** - Implementation of:
  - `BOOLPOLY` - Interactive boolean operations (Union/Intersection/Subtract)
  - `SUBPOLY` - Subtract second polyline from first
  - `INPOLY` - Intersection of two polylines
  - `UNIONPOLY` - Union of two polylines
  - `REG2POLY` - Convert region to polyline with ordered segments

#### Alignment Tools Module
- **AlignTools.h** - Header declaring alignment command functions
- **AlignTools.cpp** - Implementation of:
  - `ALX` - Align objects to same X coordinate
  - `ALY` - Align objects to same Y coordinate
  - `ALZ` - Align objects to same Z coordinate
  - Intelligent alignment by entity type (circles by center, curves by start, etc.)

## Architecture Benefits

### Separation of Concerns
Each module focuses on a specific functionality:
- **HelloWorld** - Plugin lifecycle and demo commands
- **PolylineTools** - Boolean operations and region conversions
- **AlignTools** - Object alignment utilities

### Code Organization
- Easier to locate and maintain specific functionality
- Clear module boundaries
- Reduced file size for better code navigation

### Scalability
- Easy to add new modules without modifying existing ones
- Each module can be developed and tested independently
- Clear interfaces through namespace functions

## Shared Resources

### Helper Functions
The `GetModelSpace()` function is exported from HelloWorld.cpp and declared in HelloWorld.h, making it accessible to all modules:

```cpp
extern Acad::ErrorStatus GetModelSpace(AcDbBlockTableRecord*& pModelSpace);
```

This allows modules to add entities to the drawing without duplicating code.

## Build Configuration

### Project File Updates
The HelloWorld.vcxproj file has been updated to include all module files:

**ClCompile Items:**
- HelloWorld.cpp
- PolylineTools.cpp
- AlignTools.cpp
- StdAfx.cpp (precompiled header)

**ClInclude Items:**
- HelloWorld.h
- PolylineTools.h
- AlignTools.h
- StdAfx.h

### Build Process
1. StdAfx.cpp creates precompiled headers
2. All module .cpp files are compiled in parallel
3. Files are linked into HelloWorld.arx
4. Plugin is copied to deployment locations

## Command Registration

Commands are registered in HelloWorld.cpp's `On_kInitAppMsg()` but delegate to namespace functions in respective modules:

```cpp
// Example: BOOLPOLY command
void CHelloWorldApp::booleanPolyCommand()
{
    PolylineTools::booleanPolyCommand();
}
```

This thin wrapper pattern maintains the OMF structure while keeping implementations modular.

## Available Commands

### Demo Commands
- **HELLO** - Creates a red circle with cross at specified point
- **DRAWBOX** - Creates a 3D wireframe box (blue)

### Polyline Operations
- **BOOLPOLY** - Interactive boolean (U/I/S options)
- **SUBPOLY** - Subtract polylines
- **INPOLY** - Intersect polylines
- **UNIONPOLY** - Union polylines
- **REG2POLY** - Region to polyline conversion

### Alignment Commands
- **ALX** - Align to X coordinate
- **ALY** - Align to Y coordinate
- **ALZ** - Align to Z coordinate

## Future Extensions

To add new functionality:
1. Create NewFeature.h and NewFeature.cpp
2. Add files to HelloWorld.vcxproj
3. Declare namespace functions in header
4. Implement functionality in .cpp
5. Add command wrapper in CHelloWorldApp
6. Register command in `On_kInitAppMsg()`

## Deployment

**Build Script:** Build.bat
- Compiles with Visual Studio 2022
- Deploys to:
  - `C:\Users\jissi\Documents\acadPlugins\HelloWorld.arx`
  - `C:\Users\jissi\Documents\HelloWorld.arx`

**Load in AutoCAD:**
```
Command: NETLOAD
[Select HelloWorld.arx]
```

## Technical Details

### Dependencies
- AutoCAD 2025 ObjectARX SDK
- Visual Studio 2022 Professional
- Windows SDK 10.0
- Platform Toolset v143

### Configuration
- Target: HelloWorld.arx (ObjectARX plugin)
- Platform: x64
- Configuration: Debug 2025
- Character Set: Unicode
- MFC: Dynamic linking

### Key Features
- OMF (Object Module Framework) architecture
- Namespace-based module organization
- Shared helper functions via extern declarations
- Precompiled headers for fast compilation
- Automated build and deployment

---

**Version:** 1.0 (Modular Architecture)  
**Build Date:** 2025-01-17  
**AutoCAD Version:** 2025  
**Compiler:** MSVC 17.14 (Visual Studio 2022)
