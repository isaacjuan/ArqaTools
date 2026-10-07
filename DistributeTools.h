// DistributeTools.h - Object Distribution Tools Header

#pragma once

#include "StdAfx.h"
#include <vector>

namespace DistributeTools
{
    // ── Non-interactive cores for at.distribute / at.distributeCopies (the
    // ATDIST* commands are Lua: LuaCommands/ATDISTRIBUTE.lua).
    // mode: 0 = linear (endpoints included), 1 = between (endpoints excluded),
    //       2 = equal (half spacing at each end).

    // Moves the objects (deduplicated by group, whole groups move together) so
    // their reference points are spread from startPt to endPt. Returns the
    // number of items placed, or -1 if there are too few objects or the points
    // coincide.
    int DistributeObjects(const std::vector<AcDbObjectId>& ids,
                          const AcGePoint3d& startPt, const AcGePoint3d& endPt,
                          int mode, bool verbose = true, double* spacingOut = nullptr);

    // Places `count` copies of srcId from startPt to endPt. Returns the new ids
    // (empty if count is too small for the mode or the points coincide).
    std::vector<AcDbObjectId> DistributeCopies(AcDbObjectId srcId, int count,
                                               const AcGePoint3d& startPt, const AcGePoint3d& endPt,
                                               int mode, double* spacingOut = nullptr);
}
