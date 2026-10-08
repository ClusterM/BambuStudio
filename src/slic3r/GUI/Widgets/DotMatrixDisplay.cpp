#include "DotMatrixDisplay.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <wx/dcmemory.h>
#include <wx/image.h>

namespace Slic3r { namespace GUI {

// 5x7 column-major, bit 0 = top row. Classic LED set matching the reference sheet.
static const uint8_t kGlyphEmpty[5] = {0, 0, 0, 0, 0};

static const uint8_t* glyph5x7(wxUniChar ch)
{
    // A-Z
    static const uint8_t A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t D[5] = {0x7F, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t G[5] = {0x3E, 0x41, 0x49, 0x49, 0x3A};
    static const uint8_t H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    static const uint8_t K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
    static const uint8_t R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t S[5] = {0x26, 0x49, 0x49, 0x49, 0x32};
    static const uint8_t T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t W[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
    static const uint8_t X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
    static const uint8_t Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
    static const uint8_t Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

    // 0-9
    static const uint8_t N0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t N1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t N2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t N3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t N4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t N5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t N6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t N7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t N8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t N9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};

    static const uint8_t SP[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t DT[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t MN[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t PL[5] = {0x08, 0x08, 0x3E, 0x08, 0x08};
    static const uint8_t SL[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t US[5] = {0x40, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t QH[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static const uint8_t CL[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t LP[5] = {0x00, 0x1C, 0x22, 0x41, 0x00};
    static const uint8_t RP[5] = {0x00, 0x41, 0x22, 0x1C, 0x00};
    static const uint8_t PC[5] = {0x23, 0x13, 0x08, 0x64, 0x62};

    if (ch == 'A') return A;
    if (ch == 'B') return B;
    if (ch == 'C') return C;
    if (ch == 'D') return D;
    if (ch == 'E') return E;
    if (ch == 'F') return F;
    if (ch == 'G') return G;
    if (ch == 'H') return H;
    if (ch == 'I') return I;
    if (ch == 'J') return J;
    if (ch == 'K') return K;
    if (ch == 'L') return L;
    if (ch == 'M') return M;
    if (ch == 'N') return N;
    if (ch == 'O') return O;
    if (ch == 'P') return P;
    if (ch == 'Q') return Q;
    if (ch == 'R') return R;
    if (ch == 'S') return S;
    if (ch == 'T') return T;
    if (ch == 'U') return U;
    if (ch == 'V') return V;
    if (ch == 'W') return W;
    if (ch == 'X') return X;
    if (ch == 'Y') return Y;
    if (ch == 'Z') return Z;
    if (ch == '0') return N0;
    if (ch == '1') return N1;
    if (ch == '2') return N2;
    if (ch == '3') return N3;
    if (ch == '4') return N4;
    if (ch == '5') return N5;
    if (ch == '6') return N6;
    if (ch == '7') return N7;
    if (ch == '8') return N8;
    if (ch == '9') return N9;
    if (ch == ' ') return SP;
    if (ch == '.') return DT;
    if (ch == '-') return MN;
    if (ch == '+') return PL;
    if (ch == '/') return SL;
    if (ch == '_') return US;
    if (ch == '?') return QH;
    if (ch == ':') return CL;
    if (ch == '(') return LP;
    if (ch == ')') return RP;
    if (ch == '%') return PC;
    return nullptr;
}

static bool has_glyph(wxUniChar ch)
{
    return glyph5x7(ch) != nullptr;
}

static wxString pad_center(const wxString& s)
{
    const int n = static_cast<int>(s.length());
    if (n >= DM_COLS)
        return s.Left(DM_COLS);
    // Extra column (odd leftover) goes to the left so a 6-char string in 7
    // columns does not sit against the left bezel.
    const int left  = (DM_COLS - n + 1) / 2;
    const int right = DM_COLS - n - left;
    return wxString(left, wxS(' ')) + s + wxString(right, wxS(' '));
}

wxString fit_dot_matrix_line(const wxString& raw)
{
    wxString mapped;
    mapped.reserve(raw.length());
    for (size_t i = 0; i < raw.length(); ++i) {
        wxUniChar ch = raw[i];
        const wxUint32 code = ch.GetValue();
        if (code >= 'a' && code <= 'z')
            ch = wxUniChar(code - 'a' + 'A');
        if (has_glyph(ch))
            mapped += ch;
        else
            mapped += wxS('?');
    }

    if (static_cast<int>(mapped.length()) <= DM_COLS)
        return pad_center(mapped);

    wxString compact;
    compact.reserve(mapped.length());
    for (size_t i = 0; i < mapped.length(); ++i) {
        const wxUniChar ch = mapped[i];
        if (ch != '-' && ch != ' ' && ch != '.')
            compact += ch;
    }
    if (static_cast<int>(compact.length()) <= DM_COLS)
        return pad_center(compact);
    return compact.Left(DM_COLS);
}

static bool matrix_pixel(const std::array<wxString, DM_ROWS>& fitted, int col, int row)
{
    int line = 0;
    int gy   = row;
    if (row >= DM_GLYPH_H) {
        if (row == DM_GLYPH_H)
            return false;
        line = 1;
        gy   = row - DM_GLYPH_H - DM_LINE_GAP;
    }
    if (gy < 0 || gy >= DM_GLYPH_H || line >= DM_ROWS)
        return false;

    int x = col;
    int char_idx = 0;
    for (; char_idx < DM_COLS; ++char_idx) {
        if (x < DM_GLYPH_W)
            break;
        if (x == DM_GLYPH_W)
            return false;
        x -= DM_GLYPH_W + DM_GLYPH_GAP;
    }
    if (char_idx >= DM_COLS || x < 0)
        return false;

    const wxString& text = fitted[line];
    if (char_idx >= static_cast<int>(text.length()))
        return false;

    const uint8_t* g = glyph5x7(text[char_idx]);
    if (!g)
        g = kGlyphEmpty;
    return (g[x] >> gy) & 0x01;
}

// 1:1 pixel font, then integer nearest-neighbour upscale (2x → 2×2 blocks).
// No inter-dot gap: HiDPI just makes the pixels bigger, not a sparse LED grid.
static wxImage render_glyph_grid(const std::array<wxString, DM_ROWS>& fitted)
{
    wxImage grid(DM_MATRIX_W, DM_MATRIX_H, true);
    for (int row = 0; row < DM_MATRIX_H; ++row) {
        for (int col = 0; col < DM_MATRIX_W; ++col) {
            const wxColour& c = matrix_pixel(fitted, col, row) ? DM_COLOR_DOT_ON : DM_COLOR_DOT_OFF;
            grid.SetRGB(col, row, c.Red(), c.Green(), c.Blue());
        }
    }
    return grid;
}

wxBitmap render_dot_matrix(const wxSize& target_px, const std::array<wxString, DM_ROWS>& lines, const wxColour& outside_bg)
{
    if (target_px.x <= 0 || target_px.y <= 0)
        return wxBitmap();

    std::array<wxString, DM_ROWS> fitted;
    for (int i = 0; i < DM_ROWS; ++i)
        fitted[i] = fit_dot_matrix_line(lines[i]);

    const int W = target_px.x;
    const int H = target_px.y;

    // Bezel: 1 px frame; the glyph grid is centered in what remains.
    const int inset   = 1;
    const int avail_w = std::max(1, W - inset * 2);
    const int avail_h = std::max(1, H - inset * 2);

    wxImage img(W, H, true);
    img.SetRGB(wxRect(0, 0, W, H), outside_bg.Red(), outside_bg.Green(), outside_bg.Blue());
    wxBitmap bmp(img);
    {
        wxMemoryDC mem(bmp);
        const int radius = std::max(2, std::min(W, H) / 6);
        mem.SetPen(wxPen(DM_COLOR_FRAME, inset));
        mem.SetBrush(wxBrush(DM_COLOR_BG));
        mem.DrawRoundedRectangle(0, 0, W, H, radius);
        mem.SelectObject(wxNullBitmap);
    }
    img = bmp.ConvertToImage();

    wxImage grid = render_glyph_grid(fitted);
    const int scale = std::max(1, std::min(avail_w / DM_MATRIX_W, avail_h / DM_MATRIX_H));
    if (scale > 1)
        grid.Rescale(DM_MATRIX_W * scale, DM_MATRIX_H * scale, wxIMAGE_QUALITY_NEAREST);

    img.Paste(grid, inset + (avail_w - grid.GetWidth()) / 2, inset + (avail_h - grid.GetHeight()) / 2);
    return wxBitmap(img);
}

}} // namespace Slic3r::GUI
