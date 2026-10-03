#pragma once
#include "StdAfx.h"

namespace LayerTools
{
    // Change selected objects to current layer
    void changeToCurrentLayerCommand();
    
    // Quick new layer creation and set as current
    void newLayerCommand();

    // Match layer of source object to selected objects
    void matchLayerCommand();

    // Freeze layer by selecting an object
    void freezeLayerCommand();

    // ── Non-interactive cores (used by the commands above and the Lua bindings).
    CString GetCurrentLayer();
    // Makes `name` current; with create, adds it first (color 7) if missing.
    bool SetCurrentLayer(const CString& name, bool create,
                         bool* created = nullptr, CString* err = nullptr);
    // Each state: 1 = set, 0 = clear, -1 = leave unchanged. Refuses to freeze
    // the current layer or layer 0.
    bool SetLayerState(const CString& name, int frozen, int off, int locked,
                       CString* err = nullptr);
}
