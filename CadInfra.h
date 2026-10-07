#pragma once
#include "StdAfx.h"
#include "dbmain.h"
#include "dbpl.h"
#include "dbents.h"
#include <string>
#include <vector>
#include <utility>   // std::pair

// ============================================================================
// CadInfra — AutoCAD database plumbing.
//
// All direct ObjectARX entity creation, xData read/write, and layer
// management lives here. No business/domain logic belongs in this layer.
// ============================================================================
namespace CadInfra
{
    // ── Color / lineweight utilities ─────────────────────────────────────────
    // Parse "#RRGGBB" hex string into r,g,b (0-255). Returns false if invalid.
    bool ParseHexColor(const std::string& hex, int& r, int& g, int& b);
    // Map a named color string (case-insensitive) to RGB. Returns false if unknown.
    bool NamedColorToRGB(const std::string& name, int& r, int& g, int& b);
    // Return the nearest standard ISO lineweight enum for a value in mm.
    AcDb::LineWeight MmToLineWeight(double mm);

    // ── Layer ────────────────────────────────────────────────────────────────
    // Optional properties applied to the layer record ONLY when it is first
    // created (layer_* ACML properties, spec §4.2 v6).
    struct LayerProps
    {
        std::string color;          // "" = default; "#RRGGBB" or named
        int         colorIndex = -1;    // ACI 1-255; used when `color` is empty
        std::string linetype;       // "" = Continuous
        double      lineweight = -1.0;  // -1 = default; ≥ 0 = explicit mm
        std::string description;    // "" = none
        bool        plot   = true;
        bool        locked = false;
    };

    // Creates the layer with props if it does not exist (an existing layer is
    // left untouched). Returns true only when it was created.
    bool EnsureLayer(const CString& name, const LayerProps& props = LayerProps{});

    // ── Linetype ─────────────────────────────────────────────────────────────
    // Ensures `name` is loaded in the current database's linetype table.
    // Loads from acad.lin / acadiso.lin if not already present.
    void EnsureLinetype(const CString& name);

    // ── Text height ──────────────────────────────────────────────────────────
    // Resolve the active text height: respects style fixed size and enforces
    // a units-aware minimum. Use this wherever text height is needed outside
    // of entity-insertion calls.
    double ResolveTextHeight(AcDbDatabase* pDb);

    // ── Entity insertion ─────────────────────────────────────────────────────
    // Insert a centered single-line text entity into model space.
    // rotation is in radians. Returns kNull on failure.
    AcDbObjectId InsertText(const AcGePoint3d& pos, const CString& str,
                            double rotation = 0.0,
                            const CString& layerName = CString());

    // Insert a single-line text with explicit alignment.
    AcDbObjectId InsertText(const AcGePoint3d& pos, const CString& str,
                            double rotation,
                            AcDb::TextHorzMode horzMode,
                            AcDb::TextVertMode  vertMode,
                            const CString& layerName = CString());

    // Insert an MText entity (supports \P line breaks) into model space.
    AcDbObjectId InsertMText(const AcGePoint3d& pos, const CString& str);

    // ── Geometry helpers ─────────────────────────────────────────────────────
    // Approximate centroid of a closed polyline (bounding-box centre).
    bool GetPolylineCentroid(AcDbPolyline* pPoly, AcGePoint3d& centroid);

    // ── xData persistence ────────────────────────────────────────────────────
    // App names — one per reactor type.
    extern const TCHAR* const AREA_APP_NAME;
    extern const TCHAR* const PERIM_APP_NAME;
    extern const TCHAR* const ROOM_APP_NAME;
    extern const TCHAR* const SUM_APP_NAME;
    extern const TCHAR* const LL_APP_NAME;

    void EnsureAppRegistered(AcDbDatabase* pDb, const TCHAR* appName);

    // Writes the curve -> label link as xdata under appName: the label's
    // handle, plus an optional string (ATROOMTAG keeps the room name there).
    void StoreLinkXData(AcDbObjectId curveId, AcDbObjectId labelId, const TCHAR* appName,
                        const CString* text = nullptr);

    // A curve carrying appName xdata: the label it points at (handle) and the
    // first string stored with it (ATROOMTAG keeps the room name there).
    struct XDataLink
    {
        AcDbObjectId curveId;
        AcDbObjectId labelId;
        CString      text;
    };
    // Every curve in model space of pDb that carries appName xdata.
    std::vector<XDataLink> CollectXDataLinks(AcDbDatabase* pDb, const TCHAR* appName);
}
