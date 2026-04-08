#pragma once
#include "StdAfx.h"
#include "AreaTools.h"

// ============================================================================
// ReactorPersistence — ownership, rebuild, and lifecycle of all in-memory
// transient reactors defined in AreaTools.
//
// Commands create reactors via the Register* functions.
// On DWG open the Rebuild* functions recreate them from saved xData.
// Init/Uninit are called from On_kInitAppMsg / On_kUnloadAppMsg.
// ============================================================================
namespace ReactorPersistence
{
    // ── Registration (called by commands after reactor creation) ─────────────
    void Register(PolylineAreaReactor*      r);
    void Register(PerimeterReactor*         r);
    void Register(RoomTagReactor*           r);
    void Register(PolylineSumLengthReactor* r);
    void Register(LinearLengthReactor*      r);

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    // Call from On_kInitAppMsg — attaches document reactor, rebuilds all open docs.
    void Init();

    // Call from On_kUnloadAppMsg — detaches document reactor, frees all reactors.
    void Uninit();
}
