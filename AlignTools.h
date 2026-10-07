// AlignTools.h - Alignment and Restricted Movement Tools

#pragma once

#include "StdAfx.h"
#include <vector>

namespace AlignTools
{
    // Alignment commands
    void alignXCommand();
    void alignYCommand();
    void alignZCommand();
    
    // Restricted movement commands
    void moveXCommand();
    void moveYCommand();
    void moveZCommand();
    
    // Restricted copy commands
    void copyXCommand();
    void copyYCommand();
    void copyZCommand();

    // Non-interactive core of ATALX/ATALY/ATALZ: aligns each entity (or the
    // whole group it belongs to) so its reference point sits at `coord` on
    // `axis` (0 = X, 1 = Y, 2 = Z). Returns the number of items aligned.
    int AlignObjects(const std::vector<AcDbObjectId>& ids, int axis, double coord,
                     bool verbose = true);
}
