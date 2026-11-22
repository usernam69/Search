#include "Theme.h"

#include <algorithm>
#include <cmath>

Theme g_theme;

// Sizes are given in points, as the Mac gives its own; at 100% a point is a
// pixel, and at 150% a point is one and a half. Fonts are made in pixels
// rather than points so that GDI+ doesn't apply the screen's DPI a second
// time on top of that.
static Gdiplus::Font* makeFont(const wchar_t* family, double points, double scale, int style) {
    Gdiplus::FontFamily fam(family);
    Gdiplus::FontFamily fallback(L"Segoe UI");
    Gdiplus::FontFamily& chosen = fam.IsAvailable() ? fam : fallback;
    return new Gdiplus::Font(&chosen, (Gdiplus::REAL)(points * scale), style, Gdiplus::UnitPixel);
}

void Theme::build(double scaleFactor, bool isDark) {
    scale = scaleFactor;
    dark = isDark;
    palette = Palette::resolve(isDark);
    destroy();

    // Mac's sideTitle is 12.5 points, 14 when the rows are roomy.
    title = makeFont(L"Segoe UI Variable Text", 12.5, scale, Gdiplus::FontStyleRegular);
    titleRoomy = makeFont(L"Segoe UI Variable Text", 14, scale, Gdiplus::FontStyleRegular);
    field = makeFont(L"Segoe UI Variable Text", 13.5, scale, Gdiplus::FontStyleRegular);
    suggestion = makeFont(L"Segoe UI Variable Text", 13, scale, Gdiplus::FontStyleRegular);
    smallText = makeFont(L"Segoe UI Variable Text", 11, scale, Gdiplus::FontStyleRegular);
    pin = makeFont(L"Segoe UI Variable Display", 11.5, scale, Gdiplus::FontStyleRegular);
}

void Theme::destroy() {
    for (Gdiplus::Font** f : {&title, &titleRoomy, &field, &suggestion, &smallText, &pin}) {
        delete *f;
        *f = nullptr;
    }
}

namespace draw {

using namespace Gdiplus;

static void roundedPath(GraphicsPath& path, const RectF& r, double radius) {
    double d = std::min(radius * 2, std::min((double)r.Width, (double)r.Height));
    if (d <= 0.5) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, (REAL)d, (REAL)d, 180, 90);
    path.AddArc(r.GetRight() - (REAL)d, r.Y, (REAL)d, (REAL)d, 270, 90);
    path.AddArc(r.GetRight() - (REAL)d, r.GetBottom() - (REAL)d, (REAL)d, (REAL)d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - (REAL)d, (REAL)d, (REAL)d, 90, 90);
    path.CloseFigure();
}

void roundRect(Graphics& g, const RectF& r, double radius, const Colour& fill) {
    GraphicsPath path;
    roundedPath(path, r, radius);
    SolidBrush brush(fill.gd());
    g.FillPath(&brush, &path);
}

void roundRectStroke(Graphics& g, const RectF& r, double radius, const Colour& stroke, double width) {
    GraphicsPath path;
    roundedPath(path, r, radius);
    Pen pen(stroke.gd(), (REAL)width);
    g.DrawPath(&pen, &path);
}

void text(Graphics& g, const std::wstring& s, Font* font, const Colour& colour, const RectF& box, int align,
          bool ellipsis) {
    if (s.empty() || !font) return;
    SolidBrush brush(colour.gd());
    StringFormat fmt;
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetAlignment(align == 1 ? StringAlignmentFar : align == 2 ? StringAlignmentCenter : StringAlignmentNear);
    fmt.SetTrimming(ellipsis ? StringTrimmingEllipsisCharacter : StringTrimmingNone);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    // The rendering hint is the caller's: the window paints with ClearType,
    // while a sheet or a bitmap needs grayscale, because ClearType writes no
    // alpha and nothing onto a 32-bit section.
    g.DrawString(s.c_str(), (INT)s.size(), font, box, &fmt, &brush);
}

void textLeft(Graphics& g, const std::wstring& s, Font* font, const Colour& colour, double x, double y, double w,
              double h) {
    text(g, s, font, colour, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)h), 0);
}

double textWidth(Graphics& g, const std::wstring& s, Font* font) {
    if (s.empty() || !font) return 0;
    RectF bounds;
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    g.MeasureString(s.c_str(), (INT)s.size(), font, PointF(0, 0), &fmt, &bounds);
    return bounds.Width;
}

void glyph(Graphics& g, Glyph kind, const RectF& box, const Colour& colour, double weight) {
    Pen pen(colour.gd(), (REAL)weight);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    REAL cx = box.X + box.Width / 2, cy = box.Y + box.Height / 2;
    REAL w = box.Width * 0.30f, h = box.Height * 0.30f;
    SolidBrush brush(colour.gd());

    switch (kind) {
        case Glyph::Back:
            g.DrawLine(&pen, cx + w * 0.55f, cy - h, cx - w * 0.55f, cy);
            g.DrawLine(&pen, cx - w * 0.55f, cy, cx + w * 0.55f, cy + h);
            break;
        case Glyph::Forward:
            g.DrawLine(&pen, cx - w * 0.55f, cy - h, cx + w * 0.55f, cy);
            g.DrawLine(&pen, cx + w * 0.55f, cy, cx - w * 0.55f, cy + h);
            break;
        case Glyph::Reload: {
            GraphicsPath path;
            REAL r = w * 0.9f;
            path.AddArc(cx - r, cy - r, r * 2, r * 2, -50, 285);
            g.DrawPath(&pen, &path);
            REAL a = (REAL)(-50 * 3.14159265 / 180);
            REAL ex = cx + r * cosf(a), ey = cy + r * sinf(a);
            g.FillEllipse(&brush, ex - 1.2f, ey - 1.2f, 2.4f, 2.4f);
            break;
        }
        case Glyph::Plus:
            g.DrawLine(&pen, cx - w * 0.9f, cy, cx + w * 0.9f, cy);
            g.DrawLine(&pen, cx, cy - w * 0.9f, cx, cy + w * 0.9f);
            break;
        case Glyph::Close:
            g.DrawLine(&pen, cx - w * 0.6f, cy - h * 0.6f, cx + w * 0.6f, cy + h * 0.6f);
            g.DrawLine(&pen, cx + w * 0.6f, cy - h * 0.6f, cx - w * 0.6f, cy + h * 0.6f);
            break;
        case Glyph::Search: {
            GraphicsPath path;
            REAL r = w * 0.75f;
            path.AddEllipse(cx - r - w * 0.25f, cy - r - w * 0.25f, r * 2, r * 2);
            g.DrawPath(&pen, &path);
            g.DrawLine(&pen, cx + r * 0.6f, cy + r * 0.6f, cx + r * 1.35f, cy + r * 1.35f);
            break;
        }
        case Glyph::Lock: {
            g.DrawRectangle(&pen, cx - w * 0.75f, cy - h * 0.1f, w * 1.5f, h * 1.2f);
            GraphicsPath shackle;
            shackle.AddArc(cx - w * 0.45f, cy - h * 1.05f, w * 0.9f, h * 1.1f, 180, 180);
            g.DrawPath(&pen, &shackle);
            break;
        }
        case Glyph::Speaker:
        case Glyph::SpeakerMute: {
            GraphicsPath path;
            path.AddLine(cx - w, cy - h * 0.35f, cx - w * 0.45f, cy - h * 0.35f);
            path.AddLine(cx - w * 0.45f, cy - h * 0.35f, cx + w * 0.1f, cy - h);
            path.AddLine(cx + w * 0.1f, cy - h, cx + w * 0.1f, cy + h);
            path.AddLine(cx + w * 0.1f, cy + h, cx - w * 0.45f, cy + h * 0.35f);
            path.AddLine(cx - w * 0.45f, cy + h * 0.35f, cx - w, cy + h * 0.35f);
            path.CloseFigure();
            g.FillPath(&brush, &path);
            if (kind == Glyph::Speaker) {
                GraphicsPath waves;
                waves.AddArc(cx + w * 0.1f, cy - h * 0.55f, w * 0.8f, h * 1.1f, -70, 140);
                g.DrawPath(&pen, &waves);
            } else {
                g.DrawLine(&pen, cx + w * 0.5f, cy - h * 0.45f, cx + w * 1.1f, cy + h * 0.45f);
                g.DrawLine(&pen, cx + w * 1.1f, cy - h * 0.45f, cx + w * 0.5f, cy + h * 0.45f);
            }
            break;
        }
    }
}

}  // namespace draw
