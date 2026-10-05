#include "StdAfx.h"
#include "CommandTester.h"
#include "LuaTools.h"
#include "CommonTools.h"
#include "dbents.h"
#include "dbmtext.h"
#include "dbpl.h"
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace CommandTester
{
namespace {

std::string ToUtf8(const CString& s)
{
    CT2A narrow(s, CP_UTF8);
    return std::string(static_cast<const char*>(narrow));
}

CString FromUtf8(const std::string& s)
{
    return CString(CA2T(s.c_str(), CP_UTF8));
}

// The validated file defines the command; nothing is registered in AutoCAD.
bool AcceptDefine(void*, const char*, const char*, char*, size_t)
{
    return true;
}

// Makes a fresh in-memory drawing the working database for its lifetime, so
// every at.* call of the test run lands there instead of the user's drawing.
class ScratchDrawing
{
public:
    ScratchDrawing()
        : m_prev(acdbHostApplicationServices()->workingDatabase()),
          m_db(new AcDbDatabase(/*buildDefaultDrawing=*/true, /*noDocument=*/true))
    {
        acdbHostApplicationServices()->setWorkingDatabase(m_db);
    }
    ~ScratchDrawing()
    {
        acdbHostApplicationServices()->setWorkingDatabase(m_prev);
        delete m_db;
    }
    ScratchDrawing(const ScratchDrawing&)            = delete;
    ScratchDrawing& operator=(const ScratchDrawing&) = delete;

    AcDbDatabase* db() const { return m_db; }

private:
    AcDbDatabase* m_prev;
    AcDbDatabase* m_db;
};

// Read-only Lua run against the scratch drawing after the command finished:
// what was drawn, and the mechanical defects a reviewer should not have to
// spot by eye.
const char* const kReportScript = R"LUA(
local fmt, floor, abs, huge = string.format, math.floor, math.abs, math.huge
local ents = at.entities()
if #ents == 0 then
  print("NOTHING DRAWN: the test run created no entities.")
  return
end

local minx, miny, maxx, maxy = huge, huge, -huge, -huge
local props = {}
for i, h in ipairs(ents) do
  local p = at.getProps(h)
  props[i] = p
  if p and p.min and p.max then
    minx = math.min(minx, p.min.x); miny = math.min(miny, p.min.y)
    maxx = math.max(maxx, p.max.x); maxy = math.max(maxy, p.max.y)
  end
end
local size = math.max(maxx - minx, maxy - miny)
if not (size > 0) then size = 1 end
local eps = size * 1e-6

local function n(v) return fmt("%.4g", v or 0) end
local function pt(p) return p and ("(" .. n(p.x) .. "," .. n(p.y) .. ")") or "(?)" end
local function q(v) return floor(v / eps + 0.5) end

local seen, zeroLen, retraced, duplicated = {}, 0, 0, 0
-- Registers a straight segment; returns its direction-free key (nil if zero length).
local function seg(a, b)
  if abs(a.x - b.x) <= eps and abs(a.y - b.y) <= eps then zeroLen = zeroLen + 1; return nil end
  local ka, kb = q(a.x) .. ":" .. q(a.y), q(b.x) .. ":" .. q(b.y)
  if ka > kb then ka, kb = kb, ka end
  local k = ka .. "|" .. kb
  seen[k] = (seen[k] or 0) + 1
  if seen[k] == 2 then duplicated = duplicated + 1 end
  return k
end

local counts, list, MAXLIST = {}, {}, 60
for i, h in ipairs(ents) do
  local p = props[i]
  if p then
    counts[p.type] = (counts[p.type] or 0) + 1
    local d
    if p.type == "LINE" then
      seg(p.startPoint, p.endPoint)
      d = fmt("LINE %s -> %s, length %s", pt(p.startPoint), pt(p.endPoint), n(p.length))
    elseif p.type == "LWPOLYLINE" and p.vertices then
      local v, cnt = p.vertices, #p.vertices
      local nseg = p.closed and cnt or cnt - 1
      local prev
      for s = 1, nseg do
        local a, b = v[s], v[s % cnt + 1]
        if (a.bulge or 0) == 0 then
          local k = seg(a, b)
          if k and k == prev then retraced = retraced + 1 end
          prev = k
        else
          prev = nil
        end
      end
      local vs = {}
      for j = 1, math.min(cnt, 40) do
        vs[j] = pt(v[j]) .. (((v[j].bulge or 0) ~= 0) and ("b" .. n(v[j].bulge)) or "")
      end
      d = fmt("LWPOLYLINE %s, %d vertices: %s%s", p.closed and "closed" or "open", cnt,
              table.concat(vs, " "), cnt > 40 and " ..." or "")
    elseif p.type == "CIRCLE" then
      if (p.radius or 0) <= eps then zeroLen = zeroLen + 1 end
      d = fmt("CIRCLE center %s, radius %s", pt(p.center), n(p.radius))
    elseif p.type == "ARC" then
      d = fmt("ARC center %s, radius %s, %s to %s deg", pt(p.center), n(p.radius), n(p.startAngle), n(p.endAngle))
    elseif p.type == "TEXT" or p.type == "MTEXT" then
      d = fmt("%s %q at %s, height %s", p.type, p.text or "", pt(p.position), n(p.height))
    else
      d = fmt("%s, extents %s to %s", p.type, pt(p.min), pt(p.max))
    end
    if #list < MAXLIST then list[#list + 1] = "  " .. h .. " " .. d .. " [layer " .. tostring(p.layer) .. "]" end
  end
end

local types = {}
for t, c in pairs(counts) do types[#types + 1] = c .. " " .. t end
table.sort(types)
print(fmt("ENTITIES: %d (%s)", #ents, table.concat(types, ", ")))
print(fmt("EXTENTS: %s to %s, size %s x %s", pt({x = minx, y = miny}), pt({x = maxx, y = maxy}),
          n(maxx - minx), n(maxy - miny)))
local defects = {}
if zeroLen > 0 then defects[#defects + 1] = zeroLen .. " zero-length segment(s) or zero-size circle(s) (repeated points)" end
if retraced > 0 then defects[#defects + 1] = retraced .. " polyline segment(s) going straight back over the previous segment (zig-zag)" end
if duplicated > 0 then defects[#defects + 1] = duplicated .. " straight segment(s) drawn more than once (overlapping lines)" end
if #defects == 0 then
  print("DEFECTS: none found")
else
  print("DEFECTS:")
  for _, d in ipairs(defects) do print("  - " .. d) end
end
print(fmt("GEOMETRY (WCS)%s:", #ents > MAXLIST and fmt(", first %d of %d", MAXLIST, #ents) or ""))
for _, l in ipairs(list) do print(l) end
)LUA";

// ── Plan view (GDI+) ────────────────────────────────────────────────────────
struct Path  { std::vector<AcGePoint2d> pts; };
struct Label { AcGePoint2d pos; double height = 0; double rotation = 0; CString text; bool centered = false; };

CString StripMTextCodes(const CString& s)
{
    CString out;
    for (int i = 0; i < s.GetLength(); ++i)
    {
        TCHAR ch = s[i];
        if (ch == _T('{') || ch == _T('}')) continue;
        if (ch == _T('\\') && i + 1 < s.GetLength())
        {
            TCHAR code = s[i + 1];
            if (code == _T('P') || code == _T('~')) { out += _T(' '); ++i; continue; }
            if (code == _T('\\') || code == _T('{') || code == _T('}')) { out += code; ++i; continue; }
            int semi = s.Find(_T(';'), i);   // \fArial|b0;  \H2.5x;  \C1; ...
            if (semi > i) { i = semi; continue; }
        }
        out += ch;
    }
    return out;
}

void AddCurve(AcDbCurve* c, std::vector<Path>& paths)
{
    Path p;
    if (auto* pl = AcDbPolyline::cast(c))
    {
        unsigned int n = pl->numVerts();
        unsigned int segs = pl->isClosed() ? n : (n > 0 ? n - 1 : 0);
        for (unsigned int i = 0; i < segs; ++i)
        {
            double bulge = 0.0;
            pl->getBulgeAt(i, bulge);
            int steps = bulge == 0.0 ? 1 : 24;
            for (int k = (i == 0 ? 0 : 1); k <= steps; ++k)
            {
                AcGePoint3d q;
                if (pl->getPointAtParam(i + double(k) / steps, q) == Acad::eOk)
                    p.pts.emplace_back(q.x, q.y);
            }
        }
    }
    else
    {
        double s = 0.0, e = 0.0;
        if (c->getStartParam(s) != Acad::eOk || c->getEndParam(e) != Acad::eOk) return;
        int steps = AcDbLine::cast(c) ? 1 : 96;
        for (int k = 0; k <= steps; ++k)
        {
            AcGePoint3d q;
            if (c->getPointAtParam(s + (e - s) * k / steps, q) == Acad::eOk)
                p.pts.emplace_back(q.x, q.y);
        }
    }
    if (p.pts.size() >= 2) paths.push_back(std::move(p));
}

// Curves and text as they appear in plan; anything else is exploded (block
// references, hatches, solids, ...) a few levels deep.
void Collect(AcDbEntity* e, std::vector<Path>& paths, std::vector<Label>& labels, int depth)
{
    if (auto* t = AcDbText::cast(e))
    {
        Label l;
        l.centered = t->horizontalMode() != AcDb::kTextLeft || t->verticalMode() != AcDb::kTextBase;
        AcGePoint3d at = l.centered ? t->alignmentPoint() : t->position();
        l.pos.set(at.x, at.y);
        l.height = t->height();
        l.rotation = t->rotation();
        l.text = t->textString();
        labels.push_back(l);
    }
    else if (auto* m = AcDbMText::cast(e))
    {
        Label l;
        AcString contents;
        m->contents(contents);
        l.text = StripMTextCodes(CString(contents.kwszPtr()));
        l.pos.set(m->location().x, m->location().y);
        l.height = m->textHeight();
        l.rotation = m->rotation();
        l.centered = m->attachment() == AcDbMText::kMiddleCenter;
        labels.push_back(l);
    }
    else if (auto* c = AcDbCurve::cast(e))
        AddCurve(c, paths);
    else if (depth < 3)
    {
        AcDbVoidPtrArray parts;
        if (e->explode(parts) == Acad::eOk)
            for (int i = 0; i < parts.length(); ++i)
            {
                auto* part = static_cast<AcDbEntity*>(parts[i]);
                Collect(part, paths, labels, depth + 1);
                delete part;
            }
    }
}

bool PngEncoder(CLSID& clsid)
{
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return false;
    std::vector<BYTE> buf(bytes);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { clsid = codecs[i].Clsid; return true; }
    return false;
}

// 1, 2 or 5 times a power of ten, about a fifth of `span`.
double NiceStep(double span)
{
    double raw = span / 5.0;
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double r = raw / mag;
    return (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
}

bool RenderPlan(AcDbDatabase* db, const CString& path, const CString& title, std::string& err)
{
    std::vector<Path> paths;
    std::vector<Label> labels;
    for (AcDbObjectId id : CommonTools::ModelSpaceIds(db))
    {
        AcDbEntity* e = nullptr;
        if (acdbOpenObject(e, id, AcDb::kForRead) != Acad::eOk) continue;
        Collect(e, paths, labels, 0);
        e->close();
    }
    if (paths.empty() && labels.empty()) { err = "nothing to draw"; return false; }

    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    auto grow = [&](double x, double y)
    {
        minx = (std::min)(minx, x); miny = (std::min)(miny, y);
        maxx = (std::max)(maxx, x); maxy = (std::max)(maxy, y);
    };
    for (const auto& p : paths) for (const auto& q : p.pts) grow(q.x, q.y);
    for (const auto& l : labels) { grow(l.pos.x, l.pos.y); grow(l.pos.x + l.height, l.pos.y + l.height); }
    grow(0.0, 0.0);   // keep the WCS origin (the test base point) in view
    double dx = maxx - minx, dy = maxy - miny;
    double span = (std::max)(dx, dy);
    if (!(span > 0)) span = 1;
    if (dx < span * 0.02) { minx -= span * 0.05; maxx += span * 0.05; dx = maxx - minx; }
    if (dy < span * 0.02) { miny -= span * 0.05; maxy += span * 0.05; dy = maxy - miny; }

    const int W = 1024, H = 768, margin = 48, top = 36;
    double s = (std::min)((W - 2.0 * margin) / dx, (H - top - 2.0 * margin) / dy);
    double ox = margin + (W - 2.0 * margin - dx * s) / 2;
    double oy = top + margin + (H - top - 2.0 * margin - dy * s) / 2;
    auto X = [&](double x) { return Gdiplus::REAL(ox + (x - minx) * s); };
    auto Y = [&](double y) { return Gdiplus::REAL(oy + (maxy - y) * s); };

    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) { err = "GDI+ did not start"; return false; }
    bool saved = false;
    {
        using namespace Gdiplus;
        Bitmap bmp(W, H, PixelFormat24bppRGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.Clear(Color(255, 255, 255));

        // Grid at the scale-bar step, so proportions can be read off the image.
        double step = NiceStep(span);
        Pen grid(Color(232, 232, 232), 1.0f);
        if ((dx / step) * (dy / step) < 2500)
        {
            for (double x = std::ceil(minx / step) * step; x <= maxx; x += step) g.DrawLine(&grid, X(x), Y(miny), X(x), Y(maxy));
            for (double y = std::ceil(miny / step) * step; y <= maxy; y += step) g.DrawLine(&grid, X(minx), Y(y), X(maxx), Y(y));
        }

        Pen ink(Color(20, 20, 20), 1.6f);
        for (const auto& p : paths)
        {
            std::vector<PointF> pts;
            pts.reserve(p.pts.size());
            for (const auto& q : p.pts) pts.emplace_back(X(q.x), Y(q.y));
            g.DrawLines(&ink, pts.data(), static_cast<INT>(pts.size()));
            // Mark vertices of short paths, so overlapping or doubled-back segments stand out.
            if (p.pts.size() <= 80)
            {
                SolidBrush dot(Color(30, 90, 200));
                for (const auto& f : pts) g.FillEllipse(&dot, f.X - 2.0f, f.Y - 2.0f, 4.0f, 4.0f);
            }
        }

        FontFamily family(L"Arial");
        SolidBrush textBrush(Color(0, 70, 160));
        for (const auto& l : labels)
        {
            REAL px = (std::max)(REAL(8), REAL(l.height * s));
            Gdiplus::Font font(&family, px, FontStyleRegular, UnitPixel);
            StringFormat fmt;
            if (l.centered) { fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter); }
            else fmt.SetLineAlignment(StringAlignmentFar);   // baseline-ish at the insertion point
            g.TranslateTransform(X(l.pos.x), Y(l.pos.y));
            g.RotateTransform(REAL(-l.rotation * 180.0 / 3.14159265358979));
            CStringW text(l.text);
            g.DrawString(text, -1, &font, PointF(0, 0), &fmt, &textBrush);
            g.ResetTransform();
        }

        // WCS origin = the test run's base point.
        Pen red(Color(220, 30, 30), 2.0f);
        g.DrawLine(&red, X(0) - 9, Y(0), X(0) + 9, Y(0));
        g.DrawLine(&red, X(0), Y(0) - 9, X(0), Y(0) + 9);

        Gdiplus::Font noteFont(&family, 13, FontStyleRegular, UnitPixel);
        SolidBrush black(Color(0, 0, 0));
        SolidBrush gray(Color(110, 110, 110));
        CStringW wTitle(title);
        g.DrawString(wTitle, -1, &noteFont, PointF(10, 10), &black);

        // Scale bar.
        REAL bx = 16, by = REAL(H - 18), bl = REAL(step * s);
        Pen bar(Color(0, 0, 0), 2.0f);
        g.DrawLine(&bar, bx, by, bx + bl, by);
        g.DrawLine(&bar, bx, by - 5, bx, by + 5);
        g.DrawLine(&bar, bx + bl, by - 5, bx + bl, by + 5);
        CStringW stepText;
        stepText.Format(L"%g (grid step)", step);
        g.DrawString(stepText, -1, &noteFont, PointF(bx + bl + 8, by - 9), &black);
        g.DrawString(L"plan view: +X right, +Y up; red cross = origin (0,0); blue dots = vertices",
                     -1, &noteFont, PointF(REAL(W - 470), REAL(H - 26)), &gray);

        CLSID png;
        if (!PngEncoder(png)) err = "no PNG encoder";
        else if (bmp.Save(CStringW(path), &png, nullptr) != Ok) err = "could not write " + ToUtf8(path);
        else saved = true;
    }
    Gdiplus::GdiplusShutdown(token);
    return saved;
}

} // namespace

Result Run(const CString& name, const CString& code, const CString& pngPath)
{
    Result r;
    std::string nameUtf8 = ToUtf8(name);

    LuaTools::LuaEngine engine(/*echoOutput=*/false);
    engine.setDefineCommandHandler(AcceptDefine, nullptr);
    LuaTools::LuaRunOptions loadOpts;
    loadOpts.maxInstructions = 10000000;
    engine.setLoading(true);
    LuaTools::LuaRunResult load = engine.runChunk(ToUtf8(code), "@" + nameUtf8 + ".lua", loadOpts);
    engine.setLoading(false);
    if (!load.ok) { r.skipped = _T("the file did not load: ") + FromUtf8(load.error); return r; }

    std::string reason;
    r.params = engine.sampleParams(nameUtf8, reason);
    if (r.params.empty()) { r.skipped = FromUtf8(reason); return r; }

    ScratchDrawing scratch;

    LuaTools::LuaRunOptions run;
    run.params          = r.params;
    run.answers         = "{n=0}";   // an at.get* outside the declared parameters fails instead of prompting
    run.maxInstructions = 50000000;
    run.testRun         = true;
    LuaTools::LuaRunResult res = engine.callCommand(nameUtf8, run);
    r.ran    = true;
    r.ok     = res.ok;
    r.output = res.output;
    r.error  = res.error;
    if (res.cancelled)
        r.skipped = _T("the test run was cancelled with ESC");
    else if (!res.ok && res.error.find(LuaTools::kNotInTestRun) != std::string::npos)
        r.skipped = _T("only partly test-run: it uses a function that cannot run in a scratch drawing");

    LuaTools::LuaRunOptions ro;
    ro.readOnly        = true;
    ro.maxInstructions = 50000000;
    LuaTools::LuaRunResult rep = engine.runChunk(kReportScript, "=report", ro);
    r.report = rep.ok ? rep.output : "report failed: " + rep.error;

    std::string err;
    CString title = name + _T(" test run with ") + FromUtf8(r.params);
    if (title.GetLength() > 140) title = title.Left(137) + _T("...");
    if (RenderPlan(scratch.db(), pngPath, title, err)) r.pngPath = pngPath;
    else if (err != "nothing to draw") r.report += "(plan view not rendered: " + err + ")\n";
    return r;
}

} // namespace CommandTester
