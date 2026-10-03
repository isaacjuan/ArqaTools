// DistributeTools.h - Object Distribution Tools Header

#pragma once

#include "StdAfx.h"
#include <vector>

namespace DistributeTools
{
    // Command: Distribute objects evenly along a line between two points
    void distributeLinearCommand();
    
    // Command: Distribute objects between two points (excluding endpoints)
    void distributeBetweenCommand();
    
    // Command: Distribute objects with equal spacing (half-space at ends)
    void distributeEqualCommand();

    // --- Copy-and-distribute: select ONE object, enter count, pick two points ---

    // Copies one object N times, placed from start to end (endpoints included)
    void distributeCopyLinearCommand();

    // Copies one object N times between two points (excluding endpoints)
    void distributeCopyBetweenCommand();

    // Copies one object N times with equal spacing (half-space at ends)
    void distributeCopyEqualCommand();

    // Copies one object N times distributed along a picked line entity
    void alignToLineCommand();

    // ── Non-interactive cores (used by the commands above and the Lua bindings).
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
