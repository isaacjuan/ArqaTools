// AlignTools.h - Alignment and Restricted Movement Tools

#pragma once

#include "StdAfx.h"
#include <vector>

namespace AlignTools
{
    // Non-interactive core of ATALX/ATALY/ATALZ (Lua, at.alignTo): aligns each entity (or the
    // whole group it belongs to) so its reference point sits at `coord` on
    // `axis` (0 = X, 1 = Y, 2 = Z). Returns the number of items aligned.
    int AlignObjects(const std::vector<AcDbObjectId>& ids, int axis, double coord);

    // Copies each entity, or every member of the group it belongs to (once per
    // group), moved by `delta` into model space. Group copies are not
    // re-grouped. Returns the new entities (at.copyEntities, ATCX/ATCY/ATCZ).
    std::vector<AcDbObjectId> CopyObjects(const std::vector<AcDbObjectId>& ids, const AcGeVector3d& delta);
}
