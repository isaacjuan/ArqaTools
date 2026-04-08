# HelloWorld Plugin - Quick Reload Setup

## LISP Reload Command

The `ReloadHelloWorld.lsp` file provides convenient commands for reloading the plugin during development.

### Installation

1. **Add to AutoCAD Startup Suite:**
   - Type `APPLOAD` in AutoCAD
   - Click "Contents..." button in Startup Suite section
   - Click "Add..."
   - Browse to `ReloadHelloWorld.lsp`
   - Click "Close"

2. **Or manually load each session:**
   - Type `APPLOAD` in AutoCAD
   - Browse to `ReloadHelloWorld.lsp`
   - Click "Load"

### Commands

After loading the LISP file, you have access to:

#### **RELOADHW**
Quick reload command that automatically finds and reloads HelloWorld.arx

**Usage:**
```
Command: RELOADHW
```

**How it works:**
1. Searches for HelloWorld.arx in standard locations:
   - `%USERPROFILE%\Documents\acadPlugins\HelloWorld.arx`
   - `%USERPROFILE%\Documents\HelloWorld.arx`
2. Unloads current version (if loaded)
3. Loads the latest version
4. Shows success/error messages

#### **RELOADHWPATH**
Reload with custom file browser to select plugin location

**Usage:**
```
Command: RELOADHWPATH
```

### Development Workflow

1. Make changes to code
2. Run `Build.bat` to compile
3. Type `RELOADHW` in AutoCAD
4. Test your changes immediately

### Auto-load on Startup (Optional)

To automatically load HelloWorld.arx when AutoCAD starts:

1. Open `ReloadHelloWorld.lsp`
2. Uncomment the `S::STARTUP` function (remove the `;;` from lines)
3. Save the file
4. Add to Startup Suite (see Installation above)

### Notes

- The plugin file must be closed before reloading (AutoCAD cannot replace a locked file)
- If AutoCAD is using the plugin, the old version stays locked in `acadPlugins` folder
- Build.bat copies to both locations, so reload picks up the newer copy
- Force unload (`T` parameter in `arxunload`) ensures clean reload
