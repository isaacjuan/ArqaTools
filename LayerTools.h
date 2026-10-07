#pragma once
#include "StdAfx.h"

namespace LayerTools
{
    // Non-interactive cores used by the Lua bindings (LuaTools.cpp). The
    // ATCHGTOLAYER / ATNL / ATMATCHLAYER / ATFREEZELAYER commands are Lua files
    // in the LuaCommands folder.
    CString GetCurrentLayer();
    // Makes `name` current; with create, adds it first (color 7) if missing.
    bool SetCurrentLayer(const CString& name, bool create,
                         bool* created = nullptr, CString* err = nullptr);
    // Each state: 1 = set, 0 = clear, -1 = leave unchanged. Refuses to freeze
    // the current layer or layer 0.
    bool SetLayerState(const CString& name, int frozen, int off, int locked,
                       CString* err = nullptr);
}
