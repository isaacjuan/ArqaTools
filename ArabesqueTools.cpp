// ArabesqueTools.cpp - Geometric Arabesque Pattern Generator
// Non-interactive cores; the ATARABESQUE / ATHOJANAZARI / ATARABESCO* commands
// are Lua (LuaCommands\ATARABESQUE.lua) on top of the at.pattern* bindings.

#include "StdAfx.h"
#include "ArabesqueTools.h"
#include "CommonTools.h"
#include <cmath>

static const double ARB_PI = 3.14159265358979323846;

namespace ArabesqueTools
{

    // -----------------------------------------------------------------------
    // ROSETTE  
    // N circles of radius R, each centered at distance R from origin.
    // All circles pass through the center → classic Islamic rosette.
    // -----------------------------------------------------------------------
    void DrawRosette(const AcGePoint3d& center, double R, int n)
    {
        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        for (int i = 0; i < n; i++)
        {
            double angle = 2.0 * ARB_PI * i / n;
            AcGePoint3d c(center.x + R * cos(angle),
                          center.y + R * sin(angle),
                          center.z);
            AcDbCircle* pCirc = new AcDbCircle(c, AcGeVector3d::kZAxis, R);
            CommonTools::AppendEntity(pBTR, pCirc);
        }
        pBTR->close();
    }

    // -----------------------------------------------------------------------
    // STAR
    // N-pointed star: alternating outer (R) and inner (r = R * innerFactor)
    // vertices forming a single closed polyline.
    // -----------------------------------------------------------------------
    void DrawStar(const AcGePoint3d& center, double R, int n, double innerFactor)
    {
        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        double r = R * innerFactor;
        int total = n * 2; // outer + inner vertices alternating

        AcDbPolyline* pPoly = new AcDbPolyline(total);
        for (int i = 0; i < total; i++)
        {
            // Start at top (π/2), one step = π/n (half the inter-tip angle)
            double angle = ARB_PI / 2.0 + ARB_PI * i / n;
            double rad   = (i % 2 == 0) ? R : r;
            pPoly->addVertexAt(i,
                AcGePoint2d(center.x + rad * cos(angle),
                            center.y + rad * sin(angle)),
                0.0, 0.0, 0.0);
        }
        pPoly->setClosed(Adesk::kTrue);

        CommonTools::AppendEntity(pBTR, pPoly);
        pBTR->close();
    }

    // -----------------------------------------------------------------------
    // PETALS
    // N lens-shaped petals radiating from center.
    // Each petal = 2-vertex closed polyline with equal positive bulge on both
    // segments → one arc bows left, the closing arc bows right → symmetric leaf.
    // bulge = tan(α/4):  0.4142 → 90° arc (round),  0.2679 → 60° (slender)
    // -----------------------------------------------------------------------
    void DrawPetals(const AcGePoint3d& center, double R, int n, double bulgeFactor)
    {
        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        AcGePoint2d A(center.x, center.y);

        for (int i = 0; i < n; i++)
        {
            // Petal axis angle, starting at top
            double angle = ARB_PI / 2.0 + 2.0 * ARB_PI * i / n;
            AcGePoint2d B(center.x + R * cos(angle),
                          center.y + R * sin(angle));

            AcDbPolyline* pPoly = new AcDbPolyline(2);
            // Vertex 0 → 1:   forward arc (bows to the left of direction A→B)
            pPoly->addVertexAt(0, A, bulgeFactor, 0.0, 0.0);
            // Vertex 1 → 0:   closing arc (bows to the left of direction B→A = right of A→B)
            pPoly->addVertexAt(1, B, bulgeFactor, 0.0, 0.0);
            pPoly->setClosed(Adesk::kTrue);

            CommonTools::AppendEntity(pBTR, pPoly);
        }
        pBTR->close();
    }

    // -----------------------------------------------------------------------
    // STAR ROSETTE (bonus mode "G" = Geometric)
    // Combines a star polygon with a concentric rosette ring for a richer pattern.
    // -----------------------------------------------------------------------
    void DrawGeometric(const AcGePoint3d& center, double R, int n, double innerFactor)
    {
        // Outer star
        DrawStar(center, R, n, innerFactor);

        // Inner rosette at inner radius × 0.7
        double rInner = R * innerFactor * 0.85;
        DrawRosette(center, rInner * 0.5, n);
    }

    // -----------------------------------------------------------------------
    // HOJA NAZARI (Nasrid Leaf)
    // Interlocking leaf tessellation from the Alhambra (Granada).
    //
    // Geometry:
    //   - Triangular lattice of "flower centers" with spacing = leafSize.
    //   - Basis: a1 = (s, 0),  a2 = (s/2, s*√3/2)
    //   - Center (i,j) at origin + i*a1 + j*a2
    //   - Hex distance: (|i|+|j|+|i+j|)/2, clipped to numRings
    //   - For each edge (3 forward directions), one lens-shaped leaf is drawn.
    //
    // Leaf shape:
    //   AcDbPolyline with 2 vertices + setClosed:
    //     vertex 0 = tip A,  bulge = +b  →  arc bows to the left of A→B
    //     vertex 1 = tip B,  bulge = +b  →  closing arc bows to left of B→A
    //   → symmetric lens (vesica). Bulge calculated from widthFactor.
    // -----------------------------------------------------------------------

    static void DrawLeaf(AcDbBlockTableRecord* pBTR,
                         double x1, double y1,
                         double x2, double y2,
                         double widthFactor)
    {
        double dx = x2 - x1, dy = y2 - y1;
        double len = sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return;

        // Sagitta h = half-width of leaf
        double h = len * widthFactor / 2.0;
        if (h < 1e-9) h = len * 0.01;

        // Arc radius from chord and sagitta: r = (c²+h²)/(2h), c=len/2
        double c = len / 2.0;
        double r = (c * c + h * h) / (2.0 * h);

        // Bulge = tan(half-arc-angle/2); half-arc-angle = asin(c/r)
        double halfAngle = asin(c / r);
        double bulge = tan(halfAngle / 2.0);

        AcDbPolyline* pLeaf = new AcDbPolyline(2);
        pLeaf->addVertexAt(0, AcGePoint2d(x1, y1), bulge, 0.0, 0.0);
        pLeaf->addVertexAt(1, AcGePoint2d(x2, y2), bulge, 0.0, 0.0);
        pLeaf->setClosed(Adesk::kTrue);

        CommonTools::AppendEntity(pBTR, pLeaf);
    }

    void DrawHojaNazari(const AcGePoint3d& origin,
                                double leafSize,
                                int numRings,
                                double widthFactor)
    {
        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        // Triangular lattice basis vectors
        const double a1x = leafSize;
        const double a1y = 0.0;
        const double a2x = leafSize * 0.5;
        const double a2y = leafSize * sqrt(3.0) / 2.0;

        // Three "forward" neighbor offsets in axial (i,j) coords:
        //   dir 0: (i,j) → (i+1, j  )   Δ = a1
        //   dir 1: (i,j) → (i,   j+1)   Δ = a2
        //   dir 2: (i,j) → (i+1, j-1)   Δ = a1 - a2
        const int dni[3] = { 1,  0,  1 };
        const int dnj[3] = { 0,  1, -1 };

        int range = numRings + 1;

        for (int i = -range; i <= range; i++)
        {
            for (int j = -range; j <= range; j++)
            {
                // Hex distance in axial coordinates = (|i|+|j|+|i+j|) / 2
                int hexDist = (abs(i) + abs(j) + abs(i + j)) / 2;
                if (hexDist > numRings) continue;

                double cx = origin.x + i * a1x + j * a2x;
                double cy = origin.y + i * a1y + j * a2y;

                for (int d = 0; d < 3; d++)
                {
                    int ni = i + dni[d];
                    int nj = j + dnj[d];
                    int nDist = (abs(ni) + abs(nj) + abs(ni + nj)) / 2;
                    if (nDist > numRings) continue;

                    double nx = origin.x + ni * a1x + nj * a2x;
                    double ny = origin.y + ni * a1y + nj * a2y;

                    DrawLeaf(pBTR, cx, cy, nx, ny, widthFactor);
                }
            }
        }

        pBTR->close();
    }

    // -----------------------------------------------------------------------
    // ARABESCO RETICULAR
    // Toda la geometría deriva de una sola longitud A (paso del reticulado).
    //
    // S = A*(3 + 2*sqrt(3))  ← lado de baldosa
    //
    // Construcción exacta (sin fracciones empíricas):
    //   kBot  W inferior:   P1→P2 30°, P2→P3 45°, P3→P4 45°, P4→P5 30°
    //   kTop  M superior:   espejo de kBot respecto a y = S/2
    //   kRgt  S-curva der.: P1→P2 60°v, P2→P3 45°, P3→P4 45°, P4→P5 60°v
    //   kLft  S-curva izq.: espejo de kRgt respecto a x = S/2
    //   kMain 49 nodos:     trenza principal (fracciones fijas de S)
    //
    // Fórmulas clave (r3 = sqrt(3)):
    //   Paso 30° en Y = A,   en X = A*r3
    //   Paso 45°      = 1.5*A  en X e Y
    //   X de S-curvas = A*(11-r3)/2  desde borde izquierdo  [derivado exacto]
    // -----------------------------------------------------------------------

    // 49 nodos de la trenza principal expresados como fracción de S
    static const double kMain[49][2] = {
        {0.86391,1.00000},{0.82613,0.93497},{0.51952,0.93497},
        {0.43517,0.61748},{0.12052,0.70254},{0.00531,0.50129},
        {0.12052,0.30005},{0.43517,0.38505},{0.51952,0.06771},
        {0.82666,0.06771},{0.86507,0.00064},{0.91673,0.08274},
        {0.99815,0.13477},{0.93160,0.17353},{0.93160,0.48334},
        {0.61655,0.56841},{0.70119,0.88586},{0.50515,1.00000},
        {0.30221,0.88586},{0.38658,0.56841},{0.07184,0.48334},
        {0.07184,0.17353},{0.00531,0.13477},{0.08674,0.08274},
        {0.13409,0.00000},{0.17670,0.06771},{0.48392,0.06771},
        {0.56820,0.38505},{0.88299,0.30005},{0.99567,0.49692},
        {0.88299,0.70254},{0.56820,0.61748},{0.48392,0.93497},
        {0.17670,0.93497},{0.13949,1.00000},{0.08674,0.91980},
        {0.00531,0.86772},{0.07184,0.82901},{0.07184,0.51932},
        {0.38658,0.43427},{0.30221,0.11682},{0.50002,0.00000},
        {0.70052,0.11682},{0.61655,0.43427},{0.93160,0.51932},
        {0.93160,0.82901},{0.99815,0.86772},{0.91673,0.91980},
        {0.86558,1.00000}
    };

    // Añade una polilínea abierta al espacio modelo
    static void AddOpenPoly(AcDbBlockTableRecord* pBTR,
                             const AcGePoint2dArray& pts)
    {
        int n = pts.length();
        if (n < 2) return;
        AcDbPolyline* pP = new AcDbPolyline(n);
        for (int i = 0; i < n; i++)
            pP->addVertexAt(i, pts[i], 0, 0, 0);
        CommonTools::AppendEntity(pBTR, pP);
    }

    // Dibuja una baldosa con esquina inferior-izquierda (ox, oy)
    static void DrawArabescoRlTile(AcDbBlockTableRecord* pBTR,
                                    double ox, double oy,
                                    double A, double S)
    {
        const double r3 = sqrt(3.0);
        const double s2 = S * 0.5;

        // X de las S-curvas: A*(11-r3)/2  (derivado exactamente de la geometría)
        const double xr = ox + A * (11.0 - r3) * 0.5;   // S-curva derecha
        const double xl = ox + S - A * (11.0 - r3) * 0.5; // S-curva izquierda

        AcGePoint2dArray pts;

        // ── kBot: W inferior  30°→45°→45°→30° ──────────────────────────
        pts.setLogicalLength(0);
        pts.append(AcGePoint2d(ox,                      oy + A*(2.0+r3)*0.5));
        pts.append(AcGePoint2d(ox + A*r3,               oy + A*r3*0.5));
        pts.append(AcGePoint2d(ox + A*(r3+1.5),         oy + A*(r3+3.0)*0.5));
        pts.append(AcGePoint2d(ox + A*(r3+3.0),         oy + A*r3*0.5));
        pts.append(AcGePoint2d(ox + S,                  oy + A*(2.0+r3)*0.5));
        AddOpenPoly(pBTR, pts);

        // ── kTop: M superior  (espejo de kBot) ──────────────────────────
        pts.setLogicalLength(0);
        pts.append(AcGePoint2d(ox,                      oy + S - A*(2.0+r3)*0.5));
        pts.append(AcGePoint2d(ox + A*r3,               oy + S - A*r3*0.5));
        pts.append(AcGePoint2d(ox + A*(r3+1.5),         oy + S - A*(r3+3.0)*0.5));
        pts.append(AcGePoint2d(ox + A*(r3+3.0),         oy + S - A*r3*0.5));
        pts.append(AcGePoint2d(ox + S,                  oy + S - A*(2.0+r3)*0.5));
        AddOpenPoly(pBTR, pts);

        // ── kRgt: S-curva derecha  60°v→45°→45°→60°v ───────────────────
        // P1→P2: (+A, +A*r3)  P2→P3: (-1.5A, +1.5A)
        // P3→P4: (+1.5A,+1.5A) P4→P5: (-A, +A*r3)
        pts.setLogicalLength(0);
        pts.append(AcGePoint2d(xr,           oy));
        pts.append(AcGePoint2d(xr + A,       oy + A*r3));
        pts.append(AcGePoint2d(xr - A*0.5,   oy + s2));
        pts.append(AcGePoint2d(xr + A,       oy + S - A*r3));
        pts.append(AcGePoint2d(xr,           oy + S));
        AddOpenPoly(pBTR, pts);

        // ── kLft: S-curva izquierda  (espejo de kRgt) ───────────────────
        pts.setLogicalLength(0);
        pts.append(AcGePoint2d(xl,           oy + S));
        pts.append(AcGePoint2d(xl - A,       oy + S - A*r3));
        pts.append(AcGePoint2d(xl + A*0.5,   oy + s2));
        pts.append(AcGePoint2d(xl - A,       oy + A*r3));
        pts.append(AcGePoint2d(xl,           oy));
        AddOpenPoly(pBTR, pts);

        // ── kMain: 49 nodos de la trenza principal ───────────────────────
        pts.setLogicalLength(0);
        for (int i = 0; i < 49; i++)
            pts.append(AcGePoint2d(ox + kMain[i][0]*S, oy + kMain[i][1]*S));
        AddOpenPoly(pBTR, pts);
    }

    double DrawArabescoRl(const AcGePoint3d& corner, double A, int& cols, int& rows)
    {
        if (cols < 1) cols = 1;  if (cols > 30) cols = 30;
        if (rows < 1) rows = 1;  if (rows > 30) rows = 30;

        const double S = A * (3.0 + 2.0 * sqrt(3.0));

        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return S;

        for (int row = 0; row < rows; row++)
            for (int col = 0; col < cols; col++)
                DrawArabescoRlTile(pBTR,
                                   corner.x + col * S,
                                   corner.y + row * S,
                                   A, S);
        pBTR->close();
        return S;
    }

    // -----------------------------------------------------------------------
    // ARABESCOTOROSOL - Arabesco Nazari en Toro 3D Solido
    //
    // Misma geometría de ARABESCORL, pero cada strap se proyecta sobre la
    // superficie del toro como una banda de sección rectangular:
    //   N  = normalize(P - centro_circulo_menor)   (normal al toro)
    //   T  = normalize(tangente del segmento)
    //   Bn = normalize(T x N)                      (binormal = ancho)
    //   Esquinas: P ± ww*Bn  (base),  P + hh*N ± ww*Bn  (techo)
    // Cada sub-segmento genera 3 AcDb3dFace: techo, pared izq, pared der.
    // -----------------------------------------------------------------------

    struct TsVec3 { double x, y, z; };

    static TsVec3 TsAdd(TsVec3 a, TsVec3 b) { TsVec3 r = {a.x+b.x, a.y+b.y, a.z+b.z}; return r; }
    static TsVec3 TsSub(TsVec3 a, TsVec3 b) { TsVec3 r = {a.x-b.x, a.y-b.y, a.z-b.z}; return r; }
    static TsVec3 TsMul(TsVec3 v, double s)  { TsVec3 r = {v.x*s,   v.y*s,   v.z*s};   return r; }
    static TsVec3 TsCrs(TsVec3 a, TsVec3 b) {
        TsVec3 r = { a.y*b.z - a.z*b.y,
                     a.z*b.x - a.x*b.z,
                     a.x*b.y - a.y*b.x };
        return r;
    }
    static TsVec3 TsNrm(TsVec3 v) {
        double l = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
        if (l < 1e-12) l = 1e-12;
        TsVec3 r = {v.x/l, v.y/l, v.z/l};
        return r;
    }

    // 2D tile coords (wx,wy) -> 3D point on torus (local origin at torus center)
    static TsVec3 TsPt(double wx, double wy,
                       double Rb, double Rs, double nT, double mT, double S)
    {
        double th = wx * (2.0 * ARB_PI) / (nT * S);
        double ph = wy * (2.0 * ARB_PI) / (mT * S);
        TsVec3 r = { (Rb + Rs*cos(ph))*cos(th),
                     (Rb + Rs*cos(ph))*sin(th),
                     Rs*sin(ph) };
        return r;
    }

    // Outward surface normal at P on torus
    static TsVec3 TsNml(TsVec3 P, double Rb)
    {
        double rxy = sqrt(P.x*P.x + P.y*P.y);
        if (rxy < 1e-12) rxy = 1e-12;
        TsVec3 c = { Rb*P.x/rxy, Rb*P.y/rxy, 0.0 };
        return TsNrm(TsSub(P, c));
    }

    // Append one quad face as 4 lines to the block table record
    static void TsFace(AcDbBlockTableRecord* pBTR,
                       TsVec3 p0, TsVec3 p1, TsVec3 p2, TsVec3 p3,
                       double cx, double cy, double cz)
    {
        AcGePoint3d q0(p0.x+cx, p0.y+cy, p0.z+cz);
        AcGePoint3d q1(p1.x+cx, p1.y+cy, p1.z+cz);
        AcGePoint3d q2(p2.x+cx, p2.y+cy, p2.z+cz);
        AcGePoint3d q3(p3.x+cx, p3.y+cy, p3.z+cz);
        // AcDbFace: all 4 edge-visibility flags set to kFalse = invisible (clean render)
        AcDbFace* pFace = new AcDbFace(q0, q1, q2, q3,
                                       Adesk::kFalse, Adesk::kFalse,
                                       Adesk::kFalse, Adesk::kFalse);
        CommonTools::AppendEntity(pBTR, pFace);
    }

    // Generate top + left wall + right wall faces for one strap sub-segment
    static void TsQuad(AcDbBlockTableRecord* pBTR,
                       TsVec3 pC, TsVec3 pN,
                       double Rb, double ww, double hh,
                       double cx, double cy, double cz)
    {
        TsVec3 dv = TsSub(pN, pC);
        double dlen = sqrt(dv.x*dv.x + dv.y*dv.y + dv.z*dv.z);
        if (dlen < 1e-8) return;

        TsVec3 NC  = TsNml(pC, Rb);
        TsVec3 NN  = TsNml(pN, Rb);
        TsVec3 Tc  = TsNrm(dv);

        // Cross-section corners at pC
        TsVec3 BnC = TsNrm(TsCrs(Tc, NC));
        TsVec3 TL  = TsAdd(pC, TsAdd(TsMul(NC,  hh), TsMul(BnC, -ww)));
        TsVec3 TR  = TsAdd(pC, TsAdd(TsMul(NC,  hh), TsMul(BnC,  ww)));
        TsVec3 BL  = TsAdd(pC, TsMul(BnC, -ww));
        TsVec3 BR  = TsAdd(pC, TsMul(BnC,  ww));

        // Cross-section corners at pN
        TsVec3 BnN = TsNrm(TsCrs(Tc, NN));
        TsVec3 TLn = TsAdd(pN, TsAdd(TsMul(NN,  hh), TsMul(BnN, -ww)));
        TsVec3 TRn = TsAdd(pN, TsAdd(TsMul(NN,  hh), TsMul(BnN,  ww)));
        TsVec3 BLn = TsAdd(pN, TsMul(BnN, -ww));
        TsVec3 BRn = TsAdd(pN, TsMul(BnN,  ww));

        // Techo (top), pared izq (left wall), pared der (right wall)
        TsFace(pBTR, TL,  TR,  TRn, TLn, cx, cy, cz);
        TsFace(pBTR, BL,  TL,  TLn, BLn, cx, cy, cz);
        TsFace(pBTR, TR,  BR,  BRn, TRn, cx, cy, cz);
    }

    // Walk a 2D polyline path, subdivide each segment D times, call TsQuad
    static void TsPoly(AcDbBlockTableRecord* pBTR,
                       const double pts[][2], int nPts,
                       double Rb, double Rs, double nT, double mT, double S,
                       int D, double ww, double hh,
                       double cx, double cy, double cz)
    {
        for (int k = 1; k < nPts; k++)
        {
            double wxp = pts[k-1][0], wyp = pts[k-1][1];
            double wxc = pts[k][0],   wyc = pts[k][1];
            for (int ii = 0; ii < D; ii++)
            {
                double ac = (double)ii     / (double)D;
                double an = (double)(ii+1) / (double)D;
                double wxA = wxp + ac*(wxc-wxp),  wyA = wyp + ac*(wyc-wyp);
                double wxB = wxp + an*(wxc-wxp),  wyB = wyp + an*(wyc-wyp);
                TsQuad(pBTR,
                       TsPt(wxA, wyA, Rb, Rs, nT, mT, S),
                       TsPt(wxB, wyB, Rb, Rs, nT, mT, S),
                       Rb, ww, hh, cx, cy, cz);
            }
        }
    }

    // Draw one complete arabesque tile on the torus surface
    static void TsTile(AcDbBlockTableRecord* pBTR,
                       int col, int irow, double A, double S,
                       double Rb, double Rs, double nT, double mT,
                       double cx, double cy, double cz,
                       int D, double ww, double hh)
    {
        const double r3 = sqrt(3.0);
        double ox = col  * S;
        double oy = irow * S;
        double xr = ox + A*(11.0-r3)*0.5;
        double xl = ox + S - A*(11.0-r3)*0.5;

        // kBot: W inferior  30->45->45->30
        double kBotPts[5][2] = {
            {ox,                  oy + A*(2.0+r3)*0.5},
            {ox + A*r3,           oy + A*r3*0.5},
            {ox + A*(r3+1.5),     oy + A*(r3+3.0)*0.5},
            {ox + A*(r3+3.0),     oy + A*r3*0.5},
            {ox + S,              oy + A*(2.0+r3)*0.5}
        };
        TsPoly(pBTR, kBotPts, 5, Rb, Rs, nT, mT, S, D, ww, hh, cx, cy, cz);

        // kTop: M superior (mirror of kBot)
        double kTopPts[5][2] = {
            {ox,                  oy + S - A*(2.0+r3)*0.5},
            {ox + A*r3,           oy + S - A*r3*0.5},
            {ox + A*(r3+1.5),     oy + S - A*(r3+3.0)*0.5},
            {ox + A*(r3+3.0),     oy + S - A*r3*0.5},
            {ox + S,              oy + S - A*(2.0+r3)*0.5}
        };
        TsPoly(pBTR, kTopPts, 5, Rb, Rs, nT, mT, S, D, ww, hh, cx, cy, cz);

        // kRgt: S-curve right
        double kRgtPts[5][2] = {
            {xr,         oy},
            {xr + A,     oy + A*r3},
            {xr - A*0.5, oy + S*0.5},
            {xr + A,     oy + S - A*r3},
            {xr,         oy + S}
        };
        TsPoly(pBTR, kRgtPts, 5, Rb, Rs, nT, mT, S, D, ww, hh, cx, cy, cz);

        // kLft: S-curve left (mirror of kRgt)
        double kLftPts[5][2] = {
            {xl,         oy + S},
            {xl - A,     oy + S - A*r3},
            {xl + A*0.5, oy + S*0.5},
            {xl - A,     oy + A*r3},
            {xl,         oy}
        };
        TsPoly(pBTR, kLftPts, 5, Rb, Rs, nT, mT, S, D, ww, hh, cx, cy, cz);

        // kMain: 49-node main interlace strap
        double kMainPts[49][2];
        for (int i = 0; i < 49; i++) {
            kMainPts[i][0] = ox + kMain[i][0]*S;
            kMainPts[i][1] = oy + kMain[i][1]*S;
        }
        TsPoly(pBTR, kMainPts, 49, Rb, Rs, nT, mT, S, D, ww, hh, cx, cy, cz);
    }

    void DrawArabescoToroSol(const AcGePoint3d& center, double A,
                             int nT, int mT, int D, double fw, double fh)
    {
        if (nT < 1) nT = 1;
        if (mT < 1) mT = 1;
        if (D < 2)  D = 2;

        const double S  = A * (3.0 + 2.0*sqrt(3.0));
        const double Rb = nT * S / (2.0 * ARB_PI);
        const double Rs = mT * S / (2.0 * ARB_PI);
        const double ww = A * fw * 0.5;
        const double hh = A * fh;
        const double cx = center.x, cy = center.y, cz = center.z;
        const double dnT = (double)nT, dmT = (double)mT;

        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        for (int irow = 0; irow < mT; irow++)
            for (int icol = 0; icol < nT; icol++)
                TsTile(pBTR, icol, irow, A, S, Rb, Rs, dnT, dmT,
                       cx, cy, cz, D, ww, hh);

        pBTR->close();
        acutPrintf(_T("\nToro solido: %dx%d baldosas  Rb=%.0f  Rs=%.0f  ancho=%.0f  alto=%.0f"),
                   nT, mT, Rb, Rs, 2.0*ww, hh);
    }

    // -----------------------------------------------------------------------
    // ARABESCOHIPSOL - Arabesco Nazari en Paraboloide Hiperbolico Solido
    //
    // Superficie sillon (saddle surface):
    //   z = amp * ( (xl/Lx)^2 - (yl/Ly)^2 )
    //   xl = wx - nT*S/2,   yl = wy - mT*S/2
    //   Lx = nT*S/2,        Ly = mT*S/2
    //   Normal = normalize( -2*amp*xl/Lx^2,  2*amp*yl/Ly^2,  1 )
    //
    // Reusa TsVec3, TsFace del modulo ARABESCOTOROSOL.
    // Nuevas funciones: HpPt, HpNml, HpQuad, HpPoly, HpTile.
    // -----------------------------------------------------------------------

    // 2D tile coords -> 3D point on hyperbolic paraboloid (local origin)
    static TsVec3 HpPt(double wx, double wy,
                       double nT, double mT, double S, double amp)
    {
        double Lx = nT * S * 0.5;
        double Ly = mT * S * 0.5;
        double xl = wx - Lx;
        double yl = wy - Ly;
        double u  = xl / Lx;
        double v  = yl / Ly;
        TsVec3 r  = { xl, yl, amp * (u*u - v*v) };
        return r;
    }

    // Outward surface normal at local point P on the paraboloid
    static TsVec3 HpNml(TsVec3 P, double nT, double mT, double S, double amp)
    {
        double Lx = nT * S * 0.5;
        double Ly = mT * S * 0.5;
        // grad F where F = z - amp*(x/Lx)^2 + amp*(y/Ly)^2
        double gx = -2.0 * amp * P.x / (Lx * Lx);
        double gy =  2.0 * amp * P.y / (Ly * Ly);
        TsVec3 r  = { gx, gy, 1.0 };
        return TsNrm(r);
    }

    // 3 faces (top + left wall + right wall) for one paraboloid strap sub-segment
    static void HpQuad(AcDbBlockTableRecord* pBTR,
                       TsVec3 pC, TsVec3 pN,
                       double nT, double mT, double S, double amp,
                       double ww, double hh,
                       double cx, double cy, double cz)
    {
        TsVec3 dv = TsSub(pN, pC);
        double dlen = sqrt(dv.x*dv.x + dv.y*dv.y + dv.z*dv.z);
        if (dlen < 1e-8) return;

        TsVec3 NC  = HpNml(pC, nT, mT, S, amp);
        TsVec3 NN  = HpNml(pN, nT, mT, S, amp);
        TsVec3 Tc  = TsNrm(dv);

        TsVec3 BnC = TsNrm(TsCrs(Tc, NC));
        TsVec3 TL  = TsAdd(pC, TsAdd(TsMul(NC,  hh), TsMul(BnC, -ww)));
        TsVec3 TR  = TsAdd(pC, TsAdd(TsMul(NC,  hh), TsMul(BnC,  ww)));
        TsVec3 BL  = TsAdd(pC, TsMul(BnC, -ww));
        TsVec3 BR  = TsAdd(pC, TsMul(BnC,  ww));

        TsVec3 BnN = TsNrm(TsCrs(Tc, NN));
        TsVec3 TLn = TsAdd(pN, TsAdd(TsMul(NN,  hh), TsMul(BnN, -ww)));
        TsVec3 TRn = TsAdd(pN, TsAdd(TsMul(NN,  hh), TsMul(BnN,  ww)));
        TsVec3 BLn = TsAdd(pN, TsMul(BnN, -ww));
        TsVec3 BRn = TsAdd(pN, TsMul(BnN,  ww));

        TsFace(pBTR, TL,  TR,  TRn, TLn, cx, cy, cz);
        TsFace(pBTR, BL,  TL,  TLn, BLn, cx, cy, cz);
        TsFace(pBTR, TR,  BR,  BRn, TRn, cx, cy, cz);
    }

    // Walk a 2D path, subdivide D times per segment, emit HpQuad faces
    static void HpPoly(AcDbBlockTableRecord* pBTR,
                       const double pts[][2], int nPts,
                       double nT, double mT, double S, double amp,
                       int D, double ww, double hh,
                       double cx, double cy, double cz)
    {
        for (int k = 1; k < nPts; k++)
        {
            double wxp = pts[k-1][0], wyp = pts[k-1][1];
            double wxc = pts[k][0],   wyc = pts[k][1];
            for (int ii = 0; ii < D; ii++)
            {
                double ac = (double)ii     / (double)D;
                double an = (double)(ii+1) / (double)D;
                double wxA = wxp + ac*(wxc-wxp), wyA = wyp + ac*(wyc-wyp);
                double wxB = wxp + an*(wxc-wxp), wyB = wyp + an*(wyc-wyp);
                HpQuad(pBTR,
                       HpPt(wxA, wyA, nT, mT, S, amp),
                       HpPt(wxB, wyB, nT, mT, S, amp),
                       nT, mT, S, amp, ww, hh, cx, cy, cz);
            }
        }
    }

    // Draw one complete arabesque tile on the paraboloid
    static void HpTile(AcDbBlockTableRecord* pBTR,
                       int col, int irow, double A, double S,
                       double nT, double mT, double amp,
                       double cx, double cy, double cz,
                       int D, double ww, double hh)
    {
        const double r3 = sqrt(3.0);
        double ox = col  * S;
        double oy = irow * S;
        double xr = ox + A*(11.0-r3)*0.5;
        double xl = ox + S - A*(11.0-r3)*0.5;

        double kBotPts[5][2] = {
            {ox,              oy + A*(2.0+r3)*0.5},
            {ox + A*r3,       oy + A*r3*0.5},
            {ox + A*(r3+1.5), oy + A*(r3+3.0)*0.5},
            {ox + A*(r3+3.0), oy + A*r3*0.5},
            {ox + S,          oy + A*(2.0+r3)*0.5}
        };
        HpPoly(pBTR, kBotPts, 5, nT, mT, S, amp, D, ww, hh, cx, cy, cz);

        double kTopPts[5][2] = {
            {ox,              oy + S - A*(2.0+r3)*0.5},
            {ox + A*r3,       oy + S - A*r3*0.5},
            {ox + A*(r3+1.5), oy + S - A*(r3+3.0)*0.5},
            {ox + A*(r3+3.0), oy + S - A*r3*0.5},
            {ox + S,          oy + S - A*(2.0+r3)*0.5}
        };
        HpPoly(pBTR, kTopPts, 5, nT, mT, S, amp, D, ww, hh, cx, cy, cz);

        double kRgtPts[5][2] = {
            {xr,         oy},
            {xr + A,     oy + A*r3},
            {xr - A*0.5, oy + S*0.5},
            {xr + A,     oy + S - A*r3},
            {xr,         oy + S}
        };
        HpPoly(pBTR, kRgtPts, 5, nT, mT, S, amp, D, ww, hh, cx, cy, cz);

        double kLftPts[5][2] = {
            {xl,         oy + S},
            {xl - A,     oy + S - A*r3},
            {xl + A*0.5, oy + S*0.5},
            {xl - A,     oy + A*r3},
            {xl,         oy}
        };
        HpPoly(pBTR, kLftPts, 5, nT, mT, S, amp, D, ww, hh, cx, cy, cz);

        double kMainPts[49][2];
        for (int i = 0; i < 49; i++) {
            kMainPts[i][0] = ox + kMain[i][0]*S;
            kMainPts[i][1] = oy + kMain[i][1]*S;
        }
        HpPoly(pBTR, kMainPts, 49, nT, mT, S, amp, D, ww, hh, cx, cy, cz);
    }

    void DrawArabescoHipSol(const AcGePoint3d& center, double A,
                            int nT, int mT, double fa, int D, double fw, double fh)
    {
        if (nT < 1) nT = 1;
        if (mT < 1) mT = 1;
        if (D < 2)  D = 2;

        const double S   = A * (3.0 + 2.0*sqrt(3.0));
        const double amp = A * fa;
        const double ww  = A * fw * 0.5;
        const double hh  = A * fh;
        const double cx  = center.x, cy = center.y, cz = center.z;
        const double dnT = (double)nT, dmT = (double)mT;

        AcDbBlockTableRecord* pBTR = nullptr;
        if (CommonTools::GetModelSpace(pBTR) != Acad::eOk) return;

        for (int irow = 0; irow < mT; irow++)
            for (int icol = 0; icol < nT; icol++)
                HpTile(pBTR, icol, irow, A, S, dnT, dmT, amp,
                       cx, cy, cz, D, ww, hh);

        pBTR->close();
        acutPrintf(_T("\nParaboloide: %dx%d baldosas  S=%.0f  amp=%.0f  ancho=%.0f  alto=%.0f"),
                   nT, mT, S, amp, 2.0*ww, hh);
    }

} // namespace ArabesqueTools

