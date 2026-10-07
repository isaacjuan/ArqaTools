-- Geometric arabesque patterns (ArabesqueTools):
--   ATARABESQUE        rosette / star / petals / geometric around a center
--   ATHOJANAZARI       hoja nazari (La Alhambra) hexagonal leaf tessellation
--   ATARABESCORL       arabesco andaluz 30/45 lattice, tile S = A*(3+2*sqrt(3))
--   ATARABESCOTOROSOL  the same straps as 3DFACE solids on a torus
--   ATARABESCOHIPSOL   the same straps on a hyperbolic paraboloid (saddle)
-- The geometry is at.pattern* (ArabesqueTools::Draw*); the torus and saddle
-- cores print their own summary line.
-- Was C++; edit this file and run ATLUARELOAD, no rebuild needed. Holds five
-- commands, so ATAICMD / ATLUACMDDEL leave it to be edited by hand.

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- Asks for a factor; a value outside (lo, hi) keeps the default.
local function askFactor(prompt, default, lo, hi)
    local v = at.getReal(prompt, default)
    if v and v > lo and v < hi then return v end
    return default
end

-- ── ATARABESQUE ─────────────────────────────────────────────────────────────
at.defineCommand("ATARABESQUE", function(p)
    if p.radius <= 0 then print("ATARABESQUE: Command cancelled."); return end
    local c, R = p.center, p.radius
    local n = clamp(p.units, 3, 64)

    if p.pattern == "Rosette" then
        at.patternRosette(c.x, c.y, c.z, R, n)
        print(string.format("Rosette drawn: %d interlocking circles, radius %.2f.", n, R))
    elseif p.pattern == "Star" then
        local f = askFactor("Inner radius factor (0.1-0.9)", 0.38, 0.05, 0.99)
        at.patternStar(c.x, c.y, c.z, R, n, f)
        print(string.format("Star drawn: %d points, R=%.2f, r=%.2f.", n, R, R * f))
    elseif p.pattern == "Petals" then
        -- Bulge: 0.2679 = slender (60 deg arc), 0.4142 = round (90), 0.5774 = wide (120)
        local b = askFactor("Petal fullness (0.1=slender - 0.8=wide)", 0.4142, 0.0, 1.5)
        at.patternPetals(c.x, c.y, c.z, R, n, b)
        print(string.format("Flower drawn: %d petals, radius %.2f.", n, R))
    else -- Geometric
        local f = askFactor("Inner radius factor (0.1-0.9)", 0.45, 0.05, 0.99)
        at.patternGeometric(c.x, c.y, c.z, R, n, f)
        print(string.format("Geometric pattern drawn: %d-pointed star + inner rosette.", n))
    end
end, "Draws geometric arabesque patterns: Rosette (interlocking circles), Star (n-pointed star polygon), "
  .. "Petals (lens-shaped petal flower), Geometric (star + inner rosette)", {
    { name = "center",  type = "point",    prompt = "Center point" },
    { name = "radius",  type = "distance", prompt = "Outer radius", description = "Outer radius (> 0)" },
    { name = "pattern", type = "keyword",  prompt = "Pattern [Rosette/Star/Petals/Geometric]",
      options = "Rosette Star Petals Geometric", default = "Rosette",
      description = "Star and Geometric then ask for the inner radius factor, Petals for the petal fullness" },
    { name = "units",   type = "integer",  prompt = "Number of units", default = 8,
      description = "Number of circles/points/petals, clamped to 3..64" },
})

-- ── ATHOJANAZARI ────────────────────────────────────────────────────────────
at.defineCommand("ATHOJANAZARI", function(p)
    if p.leafSize <= 0 then print("ATHOJANAZARI: Comando cancelado."); return end
    local c = p.center
    local rings = clamp(p.rings, 1, 12)
    -- Width factor = leaf max width / leaf length: 0.2 aguja, 0.35 Alhambra clasico, 0.6 hoja ancha
    local w = p.width
    if not (w > 0.05 and w < 0.85) then w = 0.35 end

    at.patternHojaNazari(c.x, c.y, c.z, p.leafSize, rings, w)
    print(string.format("Patron Hoja Nazari generado: %d anillos, tamano=%.2f, anchura=%.2f.",
                        rings, p.leafSize, w))
end, "Patron de hoja nazari hexagonal (La Alhambra): red hexagonal de centros de flor con hojas interconectadas", {
    { name = "center",   type = "point",    prompt = "Punto central" },
    { name = "leafSize", type = "distance", prompt = "Tamano de hoja (punta a punta) <500>", default = 500,
      description = "Leaf length tip to tip (> 0)" },
    { name = "rings",    type = "integer",  prompt = "Numero de anillos", default = 3,
      description = "Hexagonal rings, clamped to 1..12" },
    { name = "width",    type = "number",   prompt = "Anchura de hoja 0.1(aguja)-0.7(ancha)", default = 0.35,
      description = "Leaf width / length; outside 0.05..0.85 uses 0.35" },
})

-- ── ATARABESCORL ────────────────────────────────────────────────────────────
at.defineCommand("ATARABESCORL", function(p)
    if p.A <= 0 then print("ATARABESCORL: Cancelado."); return end
    local c = p.corner
    local cols, rows = clamp(p.cols, 1, 30), clamp(p.rows, 1, 30)
    local S = p.A * (3 + 2 * math.sqrt(3))

    at.patternArabescoRl(c.x, c.y, c.z, p.A, cols, rows)
    print(string.format("Retícula %d×%d  A=%.1f  S=%.1f", cols, rows, p.A, S))
end, "Retícula de arabesco andaluz 30/45 desde la esquina inferior izquierda; baldosa S = A*(3 + 2*sqrt(3)) ~= 6.464*A", {
    { name = "corner", type = "point",    prompt = "Esquina inferior izquierda" },
    { name = "A",      type = "distance", prompt = "Longitud fundamental A <500>", default = 500,
      description = "Fundamental length A (> 0)" },
    { name = "cols",   type = "integer",  prompt = "Columnas", default = 3, description = "Clamped to 1..30" },
    { name = "rows",   type = "integer",  prompt = "Filas",    default = 3, description = "Clamped to 1..30" },
})

-- ── ATARABESCOTOROSOL ───────────────────────────────────────────────────────
at.defineCommand("ATARABESCOTOROSOL", function(p)
    if p.A <= 0 then print("ATARABESCOTOROSOL: Cancelado."); return end
    local c = p.center
    -- Prints "Toro solido: ..." itself.
    at.patternArabescoToro(c.x, c.y, c.z, p.A, p.nT, p.mT, p.subdiv, p.widthF, p.heightF)
end, "Arabesco nazari 3D solido sobre toro (3DFACE renderable): techo + paredes laterales; "
  .. "Rb = nT*S/(2*pi), Rs = mT*S/(2*pi)", {
    { name = "center",  type = "point",    prompt = "Centro del toro" },
    { name = "A",       type = "distance", prompt = "Longitud fundamental A <100>", default = 100,
      description = "Fundamental length A (> 0)" },
    { name = "nT",      type = "integer",  prompt = "Baldosas circunferencia mayor", default = 6 },
    { name = "mT",      type = "integer",  prompt = "Baldosas circunferencia menor", default = 3 },
    { name = "subdiv",  type = "integer",  prompt = "Subdivisiones por segmento",    default = 6 },
    { name = "widthF",  type = "number",   prompt = "Ancho de strap, factor de A",   default = 0.28 },
    { name = "heightF", type = "number",   prompt = "Alto de strap, factor de A",    default = 0.08 },
})

-- ── ATARABESCOHIPSOL ────────────────────────────────────────────────────────
at.defineCommand("ATARABESCOHIPSOL", function(p)
    if p.A <= 0 then print("ATARABESCOHIPSOL: Cancelado."); return end
    local c = p.center
    -- Prints "Paraboloide: ..." itself.
    at.patternArabescoHip(c.x, c.y, c.z, p.A, p.nT, p.mT, p.ampF, p.subdiv, p.widthF, p.heightF)
end, "Arabesco nazari 3D solido sobre paraboloide hiperbolico: z = amp*((x/Lx)^2 - (y/Ly)^2), "
  .. "Lx = nT*S/2, Ly = mT*S/2, amp = A*ampF", {
    { name = "center",  type = "point",    prompt = "Centro del sillon" },
    { name = "A",       type = "distance", prompt = "Longitud fundamental A <100>", default = 100,
      description = "Fundamental length A (> 0)" },
    { name = "nT",      type = "integer",  prompt = "Baldosas en X", default = 4 },
    { name = "mT",      type = "integer",  prompt = "Baldosas en Y", default = 4 },
    { name = "ampF",    type = "number",   prompt = "Amplitud del sillon, factor de A", default = 3.0 },
    { name = "subdiv",  type = "integer",  prompt = "Subdivisiones por segmento",       default = 6 },
    { name = "widthF",  type = "number",   prompt = "Ancho de strap, factor de A",      default = 0.28 },
    { name = "heightF", type = "number",   prompt = "Alto de strap, factor de A",       default = 0.08 },
})
