# HelloWorld AutoCAD 2025 Plugin

A simple "Hello World" ObjectARX plugin demonstrating basic AutoCAD 2025 plugin development.

**Uses the same ObjectARX dependencies and structure as the beamapp project.**

## Requirements

- Visual Studio 2022 (v143 toolset)
- AutoCAD 2025
- ObjectARX SDK for AutoCAD 2025
- Environment variable `OARX2025` pointing to ObjectARX SDK (e.g., `C:\ObjectARX_for_AutoCAD_2025_Win_64bit`)

## Environment Setup

Before building, you need to set up the ObjectARX SDK path:

1. **Run the setup script**: Double-click `SetupEnvironment.bat`
   - This will create the `OARX2025` environment variable
   **Install ObjectARX SDK** for AutoCAD 2025
2. **Run `SetupEnvironment.bat`** to configure environment variables
3. **Restart Visual Studio** (if it was open)
4. **Open `HelloWorld.sln`** in Visual Studio 2022
5. **Select configuration**: `Debug 2025|x64` or `Release 2025|x64`
6. **Build** the solution (F7 or Ctrl+Shift+B)

The output will be `HelloWorld.arx` in:
- `x64\Debug 2025\HelloWorld.arx` (Debug build)
- `x64\Release 2025\HelloWorld.arx` (Release build)
     - Value: `C:\ObjectARX_for_AutoCAD_2025_Win_64bit` (or your SDK path)

## Features

- Registers a custom `HELLO` command
- Prompts user to pick a point
- Draws a red circle at the selected point
- Demonstrates basic ObjectARX API usage

## Building

1. Install ObjectARX SDK for AutoCAD 2025
2. Open `HelloWorld.sln` in Visual Studio 2022
3. Select Debug or Release configuration
4. Build (Ctrl+Shift+B)

The output will be `HelloWorld.arx` in the `x64\Debug` or `x64\Release` folder.

## Loading in AutoCAD

1. Start AutoCAD 2025
2. Type `NETLOAD` or `APPLOAD`
3. Browse to and select `HelloWorld.arx`
4. Type `HELLO` to run the command

## Usage

```
Command: HELLO
Pick a point (or press ESC): [pick a point in the drawing]
You picked point: X=100.00, Y=200.00, Z=0.00
Circle created at the selected point!
```

## Project Structure

```
HelloWorldAcad2025/
├── HelloWorld.sln          # Visual Studio solution
├── HelloWorld.vcxproj      # Project file
├── HelloWorld.def          # Module definition
├── HelloWorld.cpp          # Main implementation
├── HelloWorld.h            # Header file
├── StdAfx.h                # Precompiled header
├── StdAfx.cpp              # Precompiled header source
└── README.md               # This file
```

## Code Overview

### Main Functions

- **`initApp()`** - Called when plugin loads, registers commands
- **`unloadApp()`** - Called when plugin unloads, cleanup
- **`helloWorldCommand()`** - Implementation of the HELLO command
- **`acrxEntryPoint()`** - ObjectARX entry point

### Key APIs Used

- `acedRegCmds->addCommand()` - Register commands
- `acutPrintf()` - Print to AutoCAD command line
- `acedGetPoint()` - Get user input (point)
- `AcDbCircle` - Create circle entity
- `AcDbBlockTable` - Access block table
- `appendAcDbEntity()` - Add entity to database

## Customization

You can modify the command to:
- Draw different entities (lines, polylines, text, etc.)
- Get different types of user input
- MProject Configuration (Matches beamapp)

This project uses the **same dependency structure as beamapp**:

- **MFC Support**: Uses MFC Dynamic Library (`UseOfMfc=Dynamic`)
- **Platform**: x64 only
- **Configurations**: `Debug 2025` and `Release 2025`
- **Environment Variables**: Uses `$(OARX2025)` for SDK paths
- **Include Paths**: `$(OARX2025)\inc` and `$(OARX2025)\inc-$(Platform)`
- **Library Paths**: `$(OARX2025)\lib-$(Platform)`
- **Libraries**: `rxapi.lib`, `acdb25.lib`, `acge25.lib`, `ac1st25.lib`, `accore25.lib`, `acgiapi25.lib`
- **Preprocessor**: `WINVER=0x0A00`, `_WIN32_WINNT=0x0A00`, `_ACAD2025`, `_WINDOWS`, `_AFXEXT`
- **Precompiled Headers**: Using `StdAfx.h` with DEBUG workaround (like beamapp)

## Troubleshooting

**Environment Variable Not Set**: 
- Run `SetupEnvironment.bat`
- Restart Visual Studio
- Check: Control Panel → System → Advanced → Environment Variables

**ObjectARX SDK Not Found**: 
- Verify `OARX2025` points to correct SDK location
- Check that `%OARX2025%\inc` folder exists
- Download SDK from Autodesk Developer Network if needed

**Build Errors**: Ensure you have:
- Visual Studio 2022 with C++ development tools
- MFC component installed (C++ MFC for latest v143 build tools)
- Windows SDK 10.0
- Configuration set to `Debug 2025|x64` or `Release 2025|x64`

**"Cannot open include file"**: 
- Verify `OARX2025` environment variable is set
- Restart Visual Studio after setting environment variables
- Check that ObjectARX SDK is properly install
**Build Errors**: Ensure you have:
- Visual Studio 2022 with C++ development tools
- Windows SDK 10.0
- x64 build configuration selected

**Runtime Errors**: Verify:
- AutoCAD 2025 is installed
- Plugin is built for x64
- All required DLLs are accessible

## License

This is a simple example for educational purposes.
