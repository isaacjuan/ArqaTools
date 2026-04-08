// ArabesqueTools.h - Geometric Arabesque Pattern Generator

#pragma once

namespace ArabesqueTools
{
    // ARABESQUE - Interactive geometric pattern generator
    // Modes: [R]osette (interlocking circles)
    //        [S]tar    (n-pointed star polygon)
    //        [P]etals  (lens-shaped petal flower)
    void arabesqueCommand();

    // HOJANAZARI - Patron de hoja nazari (La Alhambra)
    // Tessellacion hexagonal de hojas (vesica) interconectadas.
    void hojaNazariCommand();

    // ARABESCORL - Retícula de arabesco andaluz 30°/45°
    // Entrada única: longitud fundamental A.  Baldosa: S = A*(3+2*sqrt(3))
    // kBot/kTop: zigzag 30-45-45-30 (ancho exacto = S)
    // kRgt/kLft: S-curva  30-45-45-30 (alto  exacto = S)
    // kMain:     49 nodos de la trenza principal
    void arabescoRlCommand();

    // ARABESCOTOROSOL - Arabesco Nazari en Toro 3D Solido
    // Los mismos straps de ARABESCORL mapeados sobre la superficie del toro,
    // con seccion rectangular: techo + pared izq + pared der (3DFACE).
    // Rb = nT*S/(2*pi),  Rs = mT*S/(2*pi)
    void arabescotoroSolCommand();

    // ARABESCOHIPSOL - Arabesco Nazari en Paraboloide Hiperbolico Solido
    // Superficie sillon: z = amp*((x/Lx)^2 - (y/Ly)^2)
    // Lx = nT*S/2,  Ly = mT*S/2,  amp = A * fa (amplitud del sillon)
    void arabescohipSolCommand();
}
