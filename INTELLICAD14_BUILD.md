# Build para IntelliCAD 14 (Simpson/hsbcad)

Guía de configuración para compilar el plugin ARX para el build interno de IntelliCAD 14
usado en el entorno Simpson/hsbcad (`C:\SST\Simpson\IntelliCAD`).

## Por qué no funciona el SDK OARX estándar

El OARX SDK (`C:\Users\jissi\hsbcad Dropbox\AcaSDK\ObjectARX 2025`) genera imports para
`acdb25.dll`, `AcPal.dll`, `AcGe25.dll`, `acui25.dll` — DLLs que **no existen** en este
build de IntelliCAD 14. En su lugar, IntelliCAD 14 usa:

| DLL presente | Equivale a |
|---|---|
| `accore.dll` | accore + IcArx (todo el API ObjectARX) |
| `IcArxGe.dll` | AcGe (geometría) |
| `IcArxBridge_25.12_17.dll` | capa de bridge |
| `IcArxUi.dll` | acui (UI) |

## Paths clave

```
SDK IcArx:   C:\SST\Simpson\IntelliCAD\Source\IntelliCAD\api\icarx\
SDK ODA:     C:\SST\Simpson\IntelliCAD\Source\IntelliCAD\api\icrx\OpenDesignSDK\
ObjectARX:   C:\SST\Simpson\IntelliCAD\Source\IntelliCAD\api\ObjectARX\
Binarios:    C:\SST\Simpson\IntelliCAD\Source\Build\VC.v143\Pro.Debug64\
Ejecutable:  C:\SST\Simpson\IntelliCAD\Source\Build\VC.v143\Pro.Debug64\Cornerstone.exe
```

Estas rutas están en `User.props` como variables MSBuild:

```xml
<ICAD14>C:\SST\Simpson\IntelliCAD\Source\IntelliCAD\api</ICAD14>
<ICAD14ODA>C:\SST\Simpson\IntelliCAD\Source\IntelliCAD\api\icrx\OpenDesignSDK</ICAD14ODA>
```

## Configuración del proyecto (HelloWorld.vcxproj)

### Include directories
```
$(ICAD14)\icarx\inc
$(ICAD14)\icarx\inc-x64
$(ICAD14ODA)
$(ICAD14ODA)\KernelBase\Include
$(ICAD14ODA)\Kernel\Include
```

### Library directories
```
$(ICAD14)\icarx\lib-x64
$(ICAD14)\ObjectARX\lib\windows-x64-vc143dbg
```

### Link dependencies
```
IcArx.lib
IcArxGe.lib
IcArxUi.lib
IcArxBridge.lib
accore.lib
acgeoment.lib
IcRxd.lib    (solo Debug)
IcRx.lib     (solo Release)
```

### Otras configuraciones críticas

| Propiedad | Valor | Motivo |
|---|---|---|
| `LanguageStandard` | `stdcpp17` | IcArx usa `requires` como nombre de método (no es keyword en C++17) |
| `UseOfMfc` | `Dynamic` | Necesario para MFC |
| `RuntimeLibrary` Debug | `MultiThreadedDebugDLL` | Debe coincidir con CRT de IcArx libs |
| `RuntimeLibrary` Release | `MultiThreadedDLL` | |
| `IgnoreSpecificDefaultLibraries` | `nafxcw.lib;nafxcwd.lib;MSVCRT.lib` | Evitar mezcla de CRT release/debug |

### NO usar en StdAfx.h
```cpp
// ELIMINAR este workaround — hace que MFC linkee mfcs140u.lib (release)
// en vez de mfcs140ud.lib (debug), causando assert en afxCurrentResourceHandle
#if defined(_DEBUG)
#define _DEBUG_WAS_DEFINED
#undef _DEBUG
#endif
```

### NO incluir OARX SDK en includes
El OARX SDK define `requires` como macro en algún header, lo que choca con C++17/C++20.
Usar solo los headers de IcArx.

## DllMain y MFC

**No definir DllMain propio.** El `mfcs140ud.lib` (linkeado automáticamente por
`UseOfMfc=Dynamic` en debug) provee el DllMain que inicializa `afxCurrentResourceHandle`.
Si se define un DllMain propio, hay conflicto de símbolo duplicado.

## Carga en IntelliCAD

```lisp
(ARXLOAD "C:/ruta/al/HelloWorld.arx")
```

O copiar el `.arx` al directorio de IntelliCAD y cargarlo desde ahí.

## Diagnóstico de DLLs faltantes

Si aparece "One of the library files needed to run cannot be found":

```bat
dumpbin /dependents HelloWorld.arx
```

Verificar que todas las DLLs listadas existan en:
- `C:\SST\Simpson\IntelliCAD\Source\Build\VC.v143\Pro.Debug64\`
- `C:\Windows\System32\`

## Nuevos módulos / archivos .cpp

Seguir el patrón existente:
1. Incluir `StdAfx.h` como primer include
2. Usar headers IcArx (`#include <inc/aced.h>` etc.) — NO headers OARX directamente
3. Agregar el `.cpp` al `HelloWorld.vcxproj`
4. Registrar comandos en `HelloWorld.cpp` → `On_kInitAppMsg`
