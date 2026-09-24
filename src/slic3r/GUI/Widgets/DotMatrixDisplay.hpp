#ifndef slic3r_GUI_DOTMATRIXDISPLAY_hpp_
#define slic3r_GUI_DOTMATRIXDISPLAY_hpp_

#include <array>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

namespace Slic3r { namespace GUI {

// Classic 5x7 LED panel palette. Tweak these three to restyle the display.
const wxColour DM_COLOR_BG(22, 23, 26);        // panel background behind the dots
const wxColour DM_COLOR_DOT_OFF(46, 48, 53);   // unlit dot
const wxColour DM_COLOR_DOT_ON(58, 255, 230);  // lit dot
// Bezel outline around the panel.
const wxColour DM_COLOR_FRAME(72, 75, 82);

// 7 columns: "K 0.030" and official types (Sup.PLA, PETG-CF, TPU-AMS) fill
// the row; leftover width becomes side padding so the grid is not left-glued.
constexpr int DM_COLS      = 7; // characters per line
constexpr int DM_ROWS      = 2; // text lines
constexpr int DM_GLYPH_W   = 5;
constexpr int DM_GLYPH_H   = 7;
constexpr int DM_GLYPH_GAP = 1; // dots between characters
constexpr int DM_LINE_GAP  = 1; // dots between lines

// Total dot grid: 7 glyphs * 5 + 6 gaps = 41 wide, 2 * 7 + 1 gap = 15 tall.
constexpr int DM_MATRIX_W = DM_COLS * DM_GLYPH_W + (DM_COLS - 1) * DM_GLYPH_GAP;
constexpr int DM_MATRIX_H = DM_ROWS * DM_GLYPH_H + (DM_ROWS - 1) * DM_LINE_GAP;

// Fit text to DM_COLS: uppercase, unknown glyphs -> '?', drop '-', ' ', '.' if
// still too long, then clip. Shorter strings are centered with spaces.
wxString fit_dot_matrix_line(const wxString& raw);

// Render a framed DM_ROWS x DM_COLS panel of exactly target_px pixels.
// Glyphs are a solid 1:1 pixel grid, integer-nearest upscaled (no LED gaps).
// outside_bg fills the corners outside the rounded bezel so the bitmap can be
// blitted opaquely onto a window with that background colour.
wxBitmap render_dot_matrix(const wxSize& target_px, const std::array<wxString, DM_ROWS>& lines, const wxColour& outside_bg);

}} // namespace Slic3r::GUI

#endif
