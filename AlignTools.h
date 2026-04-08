// AlignTools.h - Alignment and Restricted Movement Tools

#pragma once

#include "StdAfx.h"

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

    // Place object at midpoint between two points
    void placeMidCommand();
}
