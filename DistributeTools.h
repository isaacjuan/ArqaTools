// DistributeTools.h - Object Distribution Tools Header

#pragma once

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
}
