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
        std::string linetype;       // "" = Continuous
        double      lineweight = -1.0;  // -1 = default; ≥ 0 = explicit mm
        std::string description;    // "" = none
        bool        plot   = true;
        bool        locked = false;
    };

    void EnsureLayer(const CString& name, const LayerProps& props = LayerProps{});

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

    // Open polylineId and textId, recalculate area, update text string.
    bool UpdateAreaText(AcDbObjectId polylineId, AcDbObjectId textId);

    // ── xData persistence ────────────────────────────────────────────────────
    // App names — one per reactor type.
    extern const TCHAR* const AREA_APP_NAME;
    extern const TCHAR* const PERIM_APP_NAME;
    extern const TCHAR* const ROOM_APP_NAME;
    extern const TCHAR* const SUM_APP_NAME;
    extern const TCHAR* const LL_APP_NAME;

    void EnsureAppRegistered(AcDbDatabase* pDb, const TCHAR* appName);

    void StoreAreaXData        (AcDbObjectId curveId, AcDbObjectId textId);
    void StorePerimXData       (AcDbObjectId curveId, AcDbObjectId textId);
    void StoreRoomXData        (AcDbObjectId curveId, AcDbObjectId textId,
                                const CString& roomName);
    void StoreSumXData         (AcDbObjectId curveId, AcDbObjectId textId);
    void StoreLinearLengthXData(AcDbObjectId curveId, AcDbObjectId textId);

    // Scan model space of pDb and return all {curveId, textId} pairs that
    // carry xData under appName.
    void CollectXDataPairs(AcDbDatabase* pDb, const TCHAR* appName,
                           std::vector<std::pair<AcDbObjectId,
                                                 AcDbObjectId>>& pairs);
}
