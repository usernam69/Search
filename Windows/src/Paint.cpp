// What the window draws: the column, its rows and pins, the page's card, the
// field and its suggestions, and the caption buttons at the top right. And
// what it does with the pointer and the keys.
#include "Window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>

#include "Address.h"
#include "App.h"
#include "Data.h"
#include "Internal.h"
#include "Shield.h"
#include "Store.h"
#include "WebView.h"

#pragma comment(lib, "dwmapi.lib")

static constexpr int kCommandNewTab = 1;

// ---- geometry ----

void BrowserWindow::layout() {
    using namespace Gdiplus;
    lay_ = Layout();
    if (width_ <= 0 || height_ <= 0) return;

    double strip = g_theme.px(metrics::strip);
    double column = columnWidth_;

    lay_.column = RectF(0, 0, (REAL)column, (REAL)height_);

    // The page is a card: this far from the window's edges, and from the
    // column, with its corners round.
    double gap = g_theme.px(metrics::cardGap);
    lay_.page = RectF((REAL)(column + gap), (REAL)gap, (REAL)(width_ - column - gap * 2),
                      (REAL)(height_ - gap * 2));

    // The three doors at the top of the column, right of the caption buttons.
    double door = g_theme.px(metrics::helm);
    double inset = g_theme.px(metrics::sideInset);
    double doorY = (strip - door) / 2;
    double doorsX = column - inset - door * 3;
    lay_.doors.clear();
    for (int i = 0; i < 3; ++i) {
        lay_.doors.push_back(RectF((REAL)(doorsX + door * i), (REAL)doorY, (REAL)door, (REAL)door));
    }

    // The caption buttons, at the top right of the window, each 46 points
    // wide as Windows draws them.
    double captionW = g_theme.px(metrics::caption);
    lay_.caption.clear();
    for (int i = 0; i < 3; ++i) {
        lay_.caption.push_back(RectF((REAL)(width_ - captionW * (3 - i)), 0, (REAL)captionW, (REAL)strip));
    }

    // The pins: squares in a row under the band.
    auto pins = pinTabs();
    lay_.pins.clear();
    double pinSize = g_theme.px(metrics::pin);
    double pinGap = g_theme.px(metrics::pinGap);
    double pinY = strip + g_theme.px(6);
    for (size_t i = 0; i < pins.size(); ++i) {
        double x = inset + (double)i * (pinSize + pinGap);
        if (x + pinSize > column - inset) break;
        lay_.pins.push_back(RectF((REAL)x, (REAL)pinY, (REAL)pinSize, (REAL)pinSize));
    }

    // The rows, under the pins.
    auto rows = rowTabs();
    lay_.rows.clear();
    double rowH = g_theme.px(app::roomyTabs() ? metrics::sideRowRoomy : metrics::sideRow);
    double rowGap = g_theme.px(metrics::rowGap);
    double y = pinY + (lay_.pins.empty() ? 0 : pinSize + g_theme.px(10));
    for (size_t i = 0; i < rows.size(); ++i) {
        lay_.rows.push_back(RectF((REAL)inset, (REAL)y, (REAL)(column - inset * 2), (REAL)rowH));
        y += rowH + rowGap;
    }

    // The square that opens a new page, after the last row.
    lay_.newTab = RectF((REAL)inset, (REAL)(y + g_theme.px(2)), (REAL)rowH, (REAL)rowH);

    // The field, over the page when it is up: the Mac's own, in the middle of
    // the window's top, as wide as 560 points.
    double fieldW = std::min((double)g_theme.px(metrics::fieldWidth), width_ - column - g_theme.px(80));
    double fieldH = g_theme.px(38);
    lay_.field = RectF((REAL)(column + (width_ - column - fieldW) / 2), (REAL)((strip - fieldH) / 2),
                       (REAL)fieldW, (REAL)fieldH);

    // The field and its suggestions are the window's own painting, and the page
    // is WebView2's surface, composited above the window. So while the field is
    // up the page steps down below it, and comes back up when it goes.
    if (fieldShown_) {
        RectF area = fieldArea();
        double below = area.GetBottom() + (REAL)gap;
        if (below > lay_.page.Y) {
            lay_.page.Height -= below - lay_.page.Y;
            lay_.page.Y = below;
        }
    }
}

std::vector<Tab*> BrowserWindow::pinTabs() const {
    std::vector<Tab*> out;
    for (auto& tab : tabs_) {
        if (tab->isPinned()) out.push_back(tab.get());
    }
    return out;
}

std::vector<Tab*> BrowserWindow::rowTabs() const {
    std::vector<Tab*> out;
    for (auto& tab : tabs_) {
        if (!tab->isPinned()) out.push_back(tab.get());
    }
    return out;
}

void BrowserWindow::onResize(int width, int height) {
    width_ = width;
    height_ = height;
    relayout();
}

void BrowserWindow::onActivate(bool active) {
    (void)active;
    redraw();
}

void BrowserWindow::redraw() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- painting ----

void BrowserWindow::paint(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    const Palette& p = g_theme.palette;

    // The frame, then the column's own ground on it.
    SolidBrush frameBrush(p.frame.gd());
    g.FillRectangle(&frameBrush, 0, 0, width_, height_);
    SolidBrush ground(p.ground.gd());
    g.FillRectangle(&ground, 0, 0, (int)lay_.column.Width, height_);

    {

    }
    paintColumn(g);
    paintPage(g);
    // The page has stepped down out of their way, so the field and the
    // suggestions are drawn here, in the room it left.
    if (fieldShown_) {
        paintField(g);
        if (!suggestions_.empty()) paintSuggestions(g);
    }
    paintBand(g);

}

// The field, and the suggestions hanging under it, as one rectangle: this is
// the place cut out of the page and painted by the window itself.
Gdiplus::RectF BrowserWindow::fieldArea() const {
    Gdiplus::RectF box = lay_.field;
    if (!suggestions_.empty()) {
        double rowHeight = g_theme.px(46);
        Gdiplus::RectF list(lay_.field.X, lay_.field.GetBottom() + (Gdiplus::REAL)g_theme.px(6), lay_.field.Width,
                            (Gdiplus::REAL)(rowHeight * suggestions_.size() + g_theme.px(12)));
        Gdiplus::REAL left = (std::min)(box.X, list.X);
        Gdiplus::REAL top = (std::min)(box.Y, list.Y);
        Gdiplus::REAL right = (std::max)(box.GetRight(), list.GetRight());
        Gdiplus::REAL bottom = (std::max)(box.GetBottom(), list.GetBottom());
        box = Gdiplus::RectF(left, top, right - left, bottom - top);
    }
    return box;
}

void BrowserWindow::paintPage(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    // The card the page sits in. The web view is a rectangle laid over it, so
    // the corners are painted back in the frame's colour: what is left is a
    // card with its corners round.
    double radius = g_theme.px(metrics::cardRadius);
    draw::roundRect(g, lay_.page, radius, p.card);
    {
        GraphicsPath corners;
        corners.AddRectangle(lay_.page);
        GraphicsPath round;
        double d = (double)radius * 2;
        round.AddArc(lay_.page.X, lay_.page.Y, (REAL)d, (REAL)d, 180, 90);
        round.AddArc(lay_.page.GetRight() - (REAL)d, lay_.page.Y, (REAL)d, (REAL)d, 270, 90);
        round.AddArc(lay_.page.GetRight() - (REAL)d, lay_.page.GetBottom() - (REAL)d, (REAL)d, (REAL)d, 0, 90);
        round.AddArc(lay_.page.X, lay_.page.GetBottom() - (REAL)d, (REAL)d, (REAL)d, 90, 90);
        round.CloseFigure();
        corners.AddPath(&round, FALSE);
        corners.SetFillMode(FillModeAlternate);
        SolidBrush frame(p.frame.gd());
        g.FillPath(&frame, &corners);
    }

    // A page that hasn't woken yet, or one that crashed, says so on the card.
    Tab* tab = active();
    if (!tab) return;
    if (!tab->controller) {
        std::wstring text = tab->asleep ? L"Waiting to be picked" : L"Opening…";
        draw::text(g, text, g_theme.field, p.muted, lay_.page, 2, false);
    } else if (tab->crashed) {
        draw::text(g, L"This page stopped working. Press Ctrl+R to try again.", g_theme.field, p.muted, lay_.page,
                   2, false);
    }
}

void BrowserWindow::paintColumn(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    // The pins.
    auto pins = pinTabs();
    for (size_t i = 0; i < lay_.pins.size() && i < pins.size(); ++i) {
        Tab* tab = pins[i];
        bool live = tab == active();
        bool over = hot_ == Hot::Pin && hotIndex_ == (int)i;
        Colour fill = live ? p.pinLive : over ? p.sideHover : p.sideTile;
        draw::roundRect(g, lay_.pins[i], g_theme.px(7), fill);
        std::wstring letter = tab->pin.empty() ? L"•" : tab->pin.substr(0, 1);
        draw::text(g, letter, g_theme.pin, live ? p.ink : p.muted, lay_.pins[i], 2);
    }

    // The rows.
    auto rows = rowTabs();
    for (size_t i = 0; i < lay_.rows.size() && i < rows.size(); ++i) {
        paintRow(g, rows[i], lay_.rows[i], rows[i] == active(), hot_ == Hot::Row && hotIndex_ == (int)i);
    }

    // The square that opens a new page.
    if (hot_ == Hot::Plus) draw::roundRect(g, lay_.newTab, g_theme.px(7), p.hover);
    draw::glyph(g, draw::Glyph::Plus, lay_.newTab, p.muted, g_theme.px(1.3));
}

void BrowserWindow::paintRow(Gdiplus::Graphics& g, Tab* tab, const Gdiplus::RectF& row, bool live, bool over) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    if (live) {
        draw::roundRect(g, row, g_theme.px(7), p.wash);
    } else if (over) {
        draw::roundRect(g, row, g_theme.px(7), p.hover);
    }

    double inset = g_theme.px(9);
    double speakerRoom = (tab->audible || tab->muted) ? g_theme.px(24) : g_theme.px(6);
    RectF textBox(row.X + (REAL)inset, row.Y, row.Width - (REAL)(inset + speakerRoom), row.Height);
    Colour ink = live ? p.ink : tab->asleep ? p.faint : p.muted;
    draw::text(g, tab->shownTitle(), app::roomyTabs() ? g_theme.titleRoomy : g_theme.title, ink, textBox, 0);

    if (tab->audible || tab->muted) {
        RectF speaker(row.GetRight() - (REAL)g_theme.px(26), row.Y, (REAL)g_theme.px(18), row.Height);
        draw::glyph(g, tab->muted ? draw::Glyph::SpeakerMute : draw::Glyph::Speaker, speaker,
                    live ? p.ink : p.muted, g_theme.px(1.1));
    }
}

void BrowserWindow::paintBand(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    // Back, forward, reload: the three doors at the top of the column.
    Tab* tab = active();
    for (size_t i = 0; i < lay_.doors.size(); ++i) {
        bool over = hot_ == (Hot)((int)Hot::Back + (int)i);
        if (over) draw::roundRect(g, lay_.doors[i], g_theme.px(6), p.hover);
        bool enabled = i == 0 ? (tab && tab->canGoBack) : i == 1 ? (tab && tab->canGoForward) : tab != nullptr;
        Colour ink = enabled ? p.ink : p.faint;
        draw::Glyph kind = i == 0 ? draw::Glyph::Back : i == 1 ? draw::Glyph::Forward : draw::Glyph::Reload;
        draw::glyph(g, kind, lay_.doors[i], ink, g_theme.px(1.3));
    }

    // The caption buttons: minimise, maximise, close, drawn as Windows draws
    // them, faint until the pointer is on one.
    for (size_t i = 0; i < lay_.caption.size(); ++i) {
        Hot kind = i == 0 ? Hot::Minimise : i == 1 ? Hot::Maximise : Hot::Close;
        bool over = hot_ == kind;
        if (over) {
            Colour fill = i == 2 ? Colour::rgb(0.90, 0.24, 0.22)
                             : (g_theme.dark ? Colour::grey(0.26) : Colour::grey(0.93));
            SolidBrush hoverBrush(fill.gd());
            g.FillRectangle(&hoverBrush, lay_.caption[i]);
        }
        Colour ink = over && i == 2 ? Colour::grey(1.0) : p.muted;
        RectF box = lay_.caption[i];
        double s = g_theme.px(5);
        double cx = box.X + box.Width / 2, cy = box.Y + box.Height / 2;
        Pen pen(ink.gd(), 1.0f);
        switch (i) {
            case 0: g.DrawLine(&pen, (REAL)(cx - s), (REAL)cy, (REAL)(cx + s), (REAL)cy); break;
            case 1:
                if (IsZoomed(hwnd_)) {
                    g.DrawRectangle(&pen, (REAL)(cx - s + 1), (REAL)(cy - s - 1), (REAL)(s * 2 - 2), (REAL)(s * 2 - 2));
                    g.DrawLine(&pen, (REAL)(cx - s + 1), (REAL)(cy - s - 1), (REAL)(cx - s + 1), (REAL)(cy + s - 1));
                } else {
                    g.DrawRectangle(&pen, (REAL)(cx - s), (REAL)(cy - s), (REAL)(s * 2), (REAL)(s * 2));
                }
                break;
            default:
                g.DrawLine(&pen, (REAL)(cx - s), (REAL)(cy - s), (REAL)(cx + s), (REAL)(cy + s));
                g.DrawLine(&pen, (REAL)(cx + s), (REAL)(cy - s), (REAL)(cx - s), (REAL)(cy + s));
                break;
        }
    }
}

void BrowserWindow::paintField(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    // A soft shadow, then the field itself.
    for (int i = 5; i > 0; --i) {
        draw::roundRect(g, RectF(lay_.field.X - (REAL)i, lay_.field.Y - (REAL)i + g_theme.px(2),
                                 lay_.field.Width + (REAL)i * 2, lay_.field.Height + (REAL)i * 2),
                        g_theme.px(8) + i, Colour::grey(0.0, 7));
    }
    draw::roundRect(g, lay_.field, g_theme.px(8), p.popup);
    draw::roundRectStroke(g, lay_.field, g_theme.px(8), fieldFocused_ ? p.muted : p.fieldStroke, 1.0);

    // A lock for https, a magnifier for words: the field says what Return will
    // do before you press it.
    std::wstring text = fieldText_;
    std::wstring low = address::lower(text);
    bool secure = low.rfind(L"https://", 0) == 0;
    bool words = address::url(text) == std::nullopt && !text.empty();
    RectF icon(lay_.field.X + (REAL)g_theme.px(11), lay_.field.Y, (REAL)g_theme.px(18), lay_.field.Height);
    draw::glyph(g, secure ? draw::Glyph::Lock : draw::Glyph::Search, icon, p.muted, g_theme.px(1.2));

    RectF textBox(lay_.field.X + (REAL)g_theme.px(36), lay_.field.Y, lay_.field.Width - (REAL)g_theme.px(48),
                  lay_.field.Height);
    if (text.empty()) {
        draw::text(g, words ? L"Search or enter address" : L"Search or enter address", g_theme.field, p.faint,
                   textBox, 0, false);
    } else {
        // Drawn at a point rather than in a box: the field's text is one line,
        // and a layout rectangle clips whatever GDI+ thinks does not fit.
        SolidBrush ink(p.ink.gd());
        RectF bounds;
        StringFormat measure;
        measure.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
        g.MeasureString(text.c_str(), (INT)text.size(), g_theme.field, PointF(0, 0), &measure, &bounds);
        g.DrawString(text.c_str(), (INT)text.size(), g_theme.field,
                     PointF(textBox.X, textBox.Y + (textBox.Height - bounds.Height) / 2), nullptr, &ink);
    }


    // The caret.
    if (fieldFocused_ && caretOn_) {
        double before = draw::textWidth(g, text.substr(0, fieldCaret_), g_theme.field);
        double x = textBox.X + before;
        Pen caret(p.ink.gd(), 1.0f);
        g.DrawLine(&caret, (REAL)x, lay_.field.Y + (REAL)g_theme.px(9), (REAL)x,
                   lay_.field.Y + lay_.field.Height - (REAL)g_theme.px(9));
    }
}

void BrowserWindow::paintSuggestions(Gdiplus::Graphics& g) {
    using namespace Gdiplus;
    const Palette& p = g_theme.palette;

    double rowHeight = g_theme.px(46);
    RectF box(lay_.field.X, lay_.field.GetBottom() + (REAL)g_theme.px(6), lay_.field.Width,
              (REAL)(rowHeight * suggestions_.size() + g_theme.px(12)));

    for (int i = 7; i > 0; --i) {
        draw::roundRect(g, RectF(box.X - (REAL)i, box.Y - (REAL)i + g_theme.px(3), box.Width + (REAL)i * 2,
                                 box.Height + (REAL)i * 2),
                        g_theme.px(metrics::cardRadius) + i, Colour::grey(0.0, 5));
    }
    draw::roundRect(g, box, g_theme.px(metrics::cardRadius), p.popup);
    draw::roundRectStroke(g, box, g_theme.px(metrics::cardRadius), p.hairline, 1.0);

    for (size_t i = 0; i < suggestions_.size(); ++i) {
        RectF row(box.X + (REAL)g_theme.px(6), box.Y + (REAL)g_theme.px(6) + (REAL)(rowHeight * i),
                  box.Width - (REAL)g_theme.px(12), (REAL)rowHeight);
        if ((int)i == suggestionPicked_) draw::roundRect(g, row, g_theme.px(7), p.wash);

        auto& s = suggestions_[i];
        RectF icon(row.X + (REAL)g_theme.px(8), row.Y, (REAL)g_theme.px(18), row.Height);
        draw::glyph(g, s.kind == Suggestion::Kind::Search ? draw::Glyph::Search : draw::Glyph::Reload, icon,
                    p.muted, g_theme.px(1.1));

        RectF line(row.X + (REAL)g_theme.px(34), row.Y + (REAL)g_theme.px(5), row.Width - (REAL)g_theme.px(44),
                   row.Height / 2 - (REAL)g_theme.px(4));
        draw::text(g, s.title, g_theme.suggestion, p.ink, line, 0);
        RectF detail(row.X + (REAL)g_theme.px(34), row.Y + row.Height / 2 - (REAL)g_theme.px(3),
                     row.Width - (REAL)g_theme.px(44), row.Height / 2 - (REAL)g_theme.px(2));
        draw::text(g, s.detail, g_theme.smallText, p.muted, detail, 0);
    }
}
