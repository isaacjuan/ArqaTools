#include "StdAfx.h"
#include "AecTools.h"
#include "CommonTools.h"
#include "rxmfcapi.h"   // acedGetAcadWinApp
#include "axboiler.h"   // AcAxGetIUnknownOfObject (axdb.lib)

namespace AecTools
{
namespace
{
    // One standard property: COM name -> Lua field name.
    struct Spec { const wchar_t* com; const wchar_t* lua; };

    // Doors, windows, openings (Width/Height are the opening size as the
    // object's MeasureTo setting defines it).
    const Spec kOpeningSpecs[] = {
        { L"Width",       L"width" },
        { L"Height",      L"height" },
        { L"SillHeight",  L"sillHeight" },
        { L"HeadHeight",  L"headHeight" },
        { L"Rise",        L"rise" },
        { L"LeafWidth",   L"leafWidth" },
        { L"SwingAngle",  L"swingAngle" },
        { L"OpenPercent", L"openPercent" },
        { L"MeasureTo",   L"measureTo" },
        { L"StyleName",   L"style" },
    };

    const Spec kWallSpecs[] = {
        { L"Width",      L"width" },        // thickness (style or instance)
        { L"BaseHeight", L"height" },
        { L"Justify",    L"justifyCode" },
        { L"StartPoint", L"startPoint" },   // baseline, WCS
        { L"EndPoint",   L"endPoint" },
        { L"Length",     L"length" },
        { L"StyleName",  L"style" },
    };

    // Area is the space's own calculation, in the drawing's ACA area units
    // (m2 in a metric template), unlike at.getProps' generic curve area.
    const Spec kSpaceSpecs[] = {
        { L"Name",                L"name" },
        { L"Area",                L"area" },
        { L"CalculatedPerimeter", L"perimeter" },
        { L"Height",              L"height" },
        { L"Length",              L"length" },
        { L"Width",               L"width" },
        { L"Volume",              L"volume" },
        { L"Location",            L"location" },
        { L"GeometryType",        L"geometryType" },
        { L"StyleName",           L"style" },
    };

    // ACA AecWallJustification values.
    const wchar_t* WallJustifyName(int code)
    {
        switch (code)
        {
        case 0:  return L"Left";
        case 1:  return L"Center";
        case 2:  return L"Right";
        case 3:  return L"Baseline";
        default: return L"";
        }
    }

    HRESULT GetProperty(IDispatch* pDisp, const wchar_t* name, CComVariant& out)
    {
        DISPID dispId = 0;
        LPOLESTR n = const_cast<LPOLESTR>(name);
        HRESULT hr = pDisp->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &dispId);
        if (FAILED(hr)) return hr;
        DISPPARAMS noArgs = { nullptr, nullptr, 0, 0 };
        out.Clear();
        return pDisp->Invoke(dispId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                             &noArgs, &out, nullptr, nullptr);
    }

    // Reads element i of a 1-D SAFEARRAY of doubles or variants as a double.
    bool ArrayDouble(SAFEARRAY* psa, VARTYPE vt, LONG i, double& v)
    {
        if (vt == VT_R8)
            return SUCCEEDED(SafeArrayGetElement(psa, &i, &v));
        if (vt == VT_VARIANT)
        {
            CComVariant e;
            if (FAILED(SafeArrayGetElement(psa, &i, &e))) return false;
            if (FAILED(e.ChangeType(VT_R8))) return false;
            v = e.dblVal;
            return true;
        }
        return false;
    }

    // Converts a property value into a field; false = unsupported type.
    bool ToField(const CComVariant& in, Field& f)
    {
        CComVariant v;
        if (FAILED(VariantCopyInd(&v, &in))) return false;

        if (v.vt & VT_ARRAY)
        {
            SAFEARRAY* psa = v.parray;
            VARTYPE vt = static_cast<VARTYPE>(v.vt & VT_TYPEMASK);
            if (!psa || SafeArrayGetDim(psa) != 1) return false;
            LONG lo = 0, hi = -1;
            SafeArrayGetLBound(psa, 1, &lo);
            SafeArrayGetUBound(psa, 1, &hi);
            LONG n = hi - lo + 1;
            if (n != 2 && n != 3) return false;
            double c[3] = { 0.0, 0.0, 0.0 };
            for (LONG i = 0; i < n; ++i)
                if (!ArrayDouble(psa, vt, lo + i, c[i])) return false;
            f.kind = Field::Point;
            f.pt   = AcGePoint3d(c[0], c[1], c[2]);
            return true;
        }

        switch (v.vt)
        {
        case VT_BOOL:
            f.kind = Field::Bool;
            f.b    = v.boolVal != VARIANT_FALSE;
            return true;
        case VT_BSTR:
            f.kind = Field::String;
            f.str  = v.bstrVal ? CString(v.bstrVal) : CString();
            return true;
        case VT_R8: case VT_R4: case VT_I1: case VT_I2: case VT_I4: case VT_I8:
        case VT_UI1: case VT_UI2: case VT_UI4: case VT_UI8: case VT_INT: case VT_UINT:
        case VT_DECIMAL: case VT_CY:
            if (FAILED(v.ChangeType(VT_R8))) return false;
            f.kind = Field::Number;
            f.num  = v.dblVal;
            return true;
        default:
            return false;   // objects, empty, errors
        }
    }

    void ReadSpec(IDispatch* pDisp, const wchar_t* com, const wchar_t* lua, Props& out)
    {
        CComVariant v;
        if (FAILED(GetProperty(pDisp, com, v))) return;
        Field f;
        f.name = lua;
        if (ToField(v, f)) out.fields.push_back(f);
    }

    template <size_t N>
    void ReadSpecs(IDispatch* pDisp, const Spec (&specs)[N], Props& out)
    {
        for (const Spec& s : specs) ReadSpec(pDisp, s.com, s.lua, out);
    }

    CString KindOf(const CString& cls)
    {
        if (cls == _T("AecDbDoor"))    return _T("door");
        if (cls == _T("AecDbWindow"))  return _T("window");
        if (cls == _T("AecDbOpening")) return _T("opening");
        if (cls == _T("AecDbWall"))    return _T("wall");
        if (cls == _T("AecDbSpace"))   return _T("space");
        return _T("aec");
    }
}

bool IsAecClass(const AcRxClass* pClass)
{
    return pClass && pClass->name() && _tcsncmp(pClass->name(), _T("Aec"), 3) == 0;
}

bool ReadProps(AcDbObjectId id, const std::vector<CString>& extra, Props& out, CString& err)
{
    AcRxClass* pClass = id.objectClass();
    if (id.isNull() || !pClass) { err = _T("handle not found"); return false; }
    if (!IsAecClass(pClass)) { err = _T("not an AutoCAD Architecture object"); return false; }

    out.className = pClass->name();
    out.kind      = KindOf(out.className);

    {
        CommonTools::AcDbObjectGuard<AcDbEntity> ent(id);
        if (!ent) { err = _T("could not open the object"); return false; }
        AcDbExtents ext;
        if (ent->getGeomExtents(ext) == Acad::eOk)
        {
            out.hasCenter = true;
            out.center = ext.minPoint() + (ext.maxPoint() - ext.minPoint()) / 2.0;
        }
    }

    try
    {
        CWinApp* pApp = acedGetAcadWinApp();
        if (!pApp) { err = _T("AutoCAD application not available"); return false; }
        CComPtr<IDispatch> app;
        app.Attach(pApp->GetIDispatch(TRUE));
        if (!app) { err = _T("AutoCAD COM interface not available"); return false; }

        CComPtr<IUnknown> unk;
        AcDbObjectId objId = id;
        if (FAILED(AcAxGetIUnknownOfObject(&unk, objId, app)) || !unk)
        {
            err = _T("no COM object for this entity (is it in an open drawing?)");
            return false;
        }
        CComQIPtr<IDispatch> obj(unk);
        if (!obj) { err = _T("entity has no COM dispatch interface"); return false; }

        if (out.kind == _T("door") || out.kind == _T("window") || out.kind == _T("opening"))
            ReadSpecs(obj, kOpeningSpecs, out);
        else if (out.kind == _T("wall"))
        {
            ReadSpecs(obj, kWallSpecs, out);
            for (const Field& f : out.fields)
                if (f.name == _T("justifyCode") && f.kind == Field::Number)
                {
                    Field j;
                    j.name = _T("justify");
                    j.kind = Field::String;
                    j.str  = WallJustifyName(static_cast<int>(f.num));
                    out.fields.push_back(j);
                    break;
                }
        }
        else if (out.kind == _T("space"))
            ReadSpecs(obj, kSpaceSpecs, out);

        for (const CString& name : extra)
            ReadSpec(obj, name, name, out);
    }
    catch (...)
    {
        err = _T("error reading AutoCAD Architecture properties");
        return false;
    }
    return true;
}
}
