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
}
