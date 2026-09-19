#pragma once

namespace SvgExportTools
{
    // Command: SVGEXPORT — exports user-selected entities (Line, Circle, Arc,
    // LWPolyline, AcDb2dPolyline, AcDbHatch (polyline-loop fills), Text,
    // MText, Point, BlockReference) to an .svg file in the user's Documents
    // folder.
    // Block references (walls/doors/windows inserted from a block library,
    // or any other block) are expanded recursively — nested blocks (a door's
    // hardware sub-block, a dynamic block's anonymous block, etc.) are
    // expanded in turn, transformed into place, up to a generous depth cap
    // (kMaxExpansionDepth) that guards only against a cyclic/pathological
    // block definition, not legitimate nesting.
    //
    // Any other entity type (e.g. AutoCAD Architecture's AEC_WALL/AEC_DOOR/
    // AEC_WINDOW custom objects, or a proxy standing in for an unavailable
    // object enabler) falls back to AcDbEntity::explode() — the same "as
    // displayed" decomposition the EXPLODE command uses — likewise recursive
    // up to the same depth cap, since an exploded piece can itself need
    // expanding (e.g. a hatch, or another custom object).
    //
    // Prints a "[skip] <ClassName>" or explode() error line to the command
    // line for anything it could not export, so gaps are visible rather than
    // silent.
    //
    // Every element is grouped into a <g id="layer_..."> per AutoCAD layer
    // (top-level content and a block definition's own content alike), and
    // gets its own "id" derived from its source entity's handle (an
    // exploded piece, which has no handle of its own, uses its source
    // entity's id with an index suffix).
    void svgExportCommand();
}
