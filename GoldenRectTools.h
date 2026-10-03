#pragma once

namespace GoldenRectTools
{
    void goldenRectCommand();
    void goldenRectInCommand();
    void goldenRectInWCommand();

    // Non-interactive core of ATGOLDENRECT: golden-spiral rectangles whose first
    // side runs corner -> sidePt. Returns false if the points coincide.
    bool DrawGoldenSpiral(const AcGePoint3d& corner, const AcGePoint3d& sidePt);
}
