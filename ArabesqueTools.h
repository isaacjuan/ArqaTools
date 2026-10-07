// ArabesqueTools.h - Geometric Arabesque Pattern Generator

#pragma once

// The interactive commands (ATARABESQUE, ATHOJANAZARI, ATARABESCORL,
// ATARABESCOTOROSOL, ATARABESCOHIPSOL) are Lua: LuaCommands\ATARABESQUE.lua,
// built on the at.pattern* bindings in LuaTools.cpp.
namespace ArabesqueTools
{
    // ── Non-interactive pattern generators (used by the Lua at.pattern*
    //    bindings). All draw into model space.

    // Rosette: n interlocking circles. Star: n-pointed star polyline
    // (inner radius = R * innerFactor). Petals: lens-shaped petal flower.
    // Geometric: star plus inner rosette.
    void DrawRosette  (const AcGePoint3d& center, double R, int n);
    void DrawStar     (const AcGePoint3d& center, double R, int n, double innerFactor);
    void DrawPetals   (const AcGePoint3d& center, double R, int n, double bulgeFactor);
    void DrawGeometric(const AcGePoint3d& center, double R, int n, double innerFactor);

    // Hoja nazari (La Alhambra): hexagonal tessellation of interlocking leaves.
    void DrawHojaNazari(const AcGePoint3d& origin, double leafSize, int numRings, double widthFactor);

    // Arabesco reticular 30/45: tile S = A*(3+2*sqrt(3)), lower-left corner.
    // Clamps cols/rows to 1..30 in place; returns the tile size S.
    double DrawArabescoRl(const AcGePoint3d& corner, double A, int& cols, int& rows);

    // The same straps mapped onto a torus (Rb = nT*S/(2*pi), Rs = mT*S/(2*pi))
    // or a hyperbolic paraboloid (z = amp*((x/Lx)^2 - (y/Ly)^2), amp = A*fa),
    // with a rectangular section (3DFACE). Both print a summary line.
    void DrawArabescoToroSol(const AcGePoint3d& center, double A,
                             int nT, int mT, int D, double fw, double fh);
    void DrawArabescoHipSol(const AcGePoint3d& center, double A,
                            int nT, int mT, double fa, int D, double fw, double fh);
}
