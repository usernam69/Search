// The app's colours and measurements: Design.swift's own values, so the
// Windows build looks like the same app as the Mac one. Every colour is a
// light/dark pair, resolved once from the system's setting.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>

struct Colour {
    BYTE r = 0, g = 0, b = 0, a = 255;

    COLORREF ref() const { return RGB(r, g, b); }
    Gdiplus::Color gd() const { return Gdiplus::Color(a, r, g, b); }

    static Colour grey(double white, BYTE alpha = 255) {
        BYTE v = (BYTE)(white * 255.0 + 0.5);
        return Colour{v, v, v, alpha};
    }
    static Colour rgb(double r, double g, double b, BYTE alpha = 255) {
        return Colour{(BYTE)(r * 255.0 + 0.5), (BYTE)(g * 255.0 + 0.5), (BYTE)(b * 255.0 + 0.5), alpha};
    }
};

// Palette's pairs, by name, in the order Design.swift lists them.
struct Palette {
    Colour ground, ink, muted, faint, hairline, wash, pinLive, hover, frame;
    Colour sideWash, sideHover, sideTile, sidePinLive;
    Colour safe, unsafe;
    // Not on the Mac: the page's card in a dark window sits a step above the
    // frame, and the address field needs a fill of its own.
    Colour card, field, fieldStroke, popup, shadow;

    static Palette resolve(bool dark) {
        Palette p;
        auto pair = [dark](double light, double dim) { return Colour::grey(dark ? dim : light); };
        p.ground = pair(1.0, 0.11);
        p.ink = pair(0.09, 0.93);
        p.muted = pair(0.55, 0.58);
        p.faint = pair(0.83, 0.32);
        p.hairline = pair(0.91, 0.20);
        p.wash = pair(0.937, 0.175);
        p.pinLive = pair(0.90, 0.21);
        p.hover = pair(0.965, 0.15);
        p.frame = pair(0.955, 0.075);
        p.sideWash = pair(0.895, 0.155);
        p.sideHover = pair(0.915, 0.13);
        p.sideTile = pair(0.925, 0.115);
        p.sidePinLive = pair(0.86, 0.19);
        p.safe = dark ? Colour::rgb(0.29, 0.87, 0.50) : Colour::rgb(0.08, 0.50, 0.24);
        p.unsafe = dark ? Colour::rgb(0.98, 0.75, 0.14) : Colour::rgb(0.71, 0.33, 0.04);
        p.card = pair(1.0, 0.145);
        p.field = pair(0.955, 0.175);
        p.fieldStroke = pair(0.895, 0.24);
        p.popup = pair(1.0, 0.16);
        p.shadow = Colour::grey(0.0, dark ? 90 : 26);
        return p;
    }
};

// The Mac's Metrics, in points.
namespace metrics {
constexpr int strip = 52;
constexpr int cardGap = 8;
constexpr int cardRadius = 10;
constexpr int side = 232;
constexpr int sideMin = 176;
constexpr int sideMax = 440;
constexpr int sideRow = 28;
constexpr int sideRowRoomy = 36;
constexpr int pin = 30;
constexpr int plus = 30;
constexpr int helm = 26;
constexpr int fieldWidth = 560;
constexpr int sideInset = 10;
constexpr int rowGap = 2;
constexpr int pinGap = 4;
constexpr int caption = 46;   // a caption button's width on Windows
}  // namespace metrics

// The fonts, in points, and the scale factor the system is running at.
struct Theme {
    double scale = 1.0;
    bool dark = false;
    Palette palette;

    Gdiplus::Font* title = nullptr;
    Gdiplus::Font* titleRoomy = nullptr;
    Gdiplus::Font* field = nullptr;
    Gdiplus::Font* suggestion = nullptr;
    Gdiplus::Font* smallText = nullptr;
    Gdiplus::Font* pin = nullptr;

    void build(double scaleFactor, bool isDark);
    void destroy();

    int px(double points) const { return (int)(points * scale + 0.5); }
    int pt(double points) const { return (int)(points * scale + 0.5); }
};

extern Theme g_theme;

namespace draw {
void roundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, double radius, const Colour& fill);
void roundRectStroke(Gdiplus::Graphics& g, const Gdiplus::RectF& r, double radius, const Colour& stroke,
                     double width);
void text(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* font, const Colour& colour,
          const Gdiplus::RectF& box, int align = 0, bool ellipsis = true);
void textLeft(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* font, const Colour& colour,
              double x, double y, double w, double h);
double textWidth(Gdiplus::Graphics& g, const std::wstring& s, Gdiplus::Font* font);

enum class Glyph { Back, Forward, Reload, Plus, Close, Search, Lock, Speaker, SpeakerMute };
void glyph(Gdiplus::Graphics& g, Glyph kind, const Gdiplus::RectF& box, const Colour& colour, double weight);
}  // namespace draw
