#include "renderer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    HDC screen;
    HDC dest_dc;
    HDC mask_dc;
    HBITMAP dest_bitmap;
    HBITMAP mask_bitmap;
    HGDIOBJ old_dest;
    HGDIOBJ old_mask;
    uint32_t *pixels;
    uint32_t *mask;
    uint32_t *base;
    HFONT font;
    int width;
    int height;
    UINT dpi;
} RendererState;

static RendererState g_renderer;

static int ClampByte(int value) {
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

static uint32_t PackPixel(int r, int g, int b, int a) {
    r = ClampByte(r) * a / 255;
    g = ClampByte(g) * a / 255;
    b = ClampByte(b) * a / 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t BlendPixel(uint32_t dst, int r, int g, int b, int a) {
    int da = (int)(dst >> 24);
    int dr = (int)((dst >> 16) & 255);
    int dg = (int)((dst >> 8) & 255);
    int db = (int)(dst & 255);
    int inv = 255 - a;
    int oa = a + da * inv / 255;
    int orv = r * a / 255 + dr * inv / 255;
    int og = g * a / 255 + dg * inv / 255;
    int ob = b * a / 255 + db * inv / 255;
    return ((uint32_t)oa << 24) | ((uint32_t)orv << 16) |
           ((uint32_t)og << 8) | (uint32_t)ob;
}

static BOOL InRoundedRect(int x, int y, int left, int top, int right, int bottom, int radius) {
    if (x < left || x >= right || y < top || y >= bottom) return FALSE;
    int cx = x < left + radius ? left + radius - x :
             (x >= right - radius ? x - (right - radius - 1) : 0);
    int cy = y < top + radius ? top + radius - y :
             (y >= bottom - radius ? y - (bottom - radius - 1) : 0);
    return !(cx && cy && cx * cx + cy * cy > radius * radius);
}

static void DestroyBuffers(void) {
    if (g_renderer.dest_dc && g_renderer.old_dest) SelectObject(g_renderer.dest_dc, g_renderer.old_dest);
    if (g_renderer.mask_dc && g_renderer.old_mask) SelectObject(g_renderer.mask_dc, g_renderer.old_mask);
    if (g_renderer.dest_bitmap) DeleteObject(g_renderer.dest_bitmap);
    if (g_renderer.mask_bitmap) DeleteObject(g_renderer.mask_bitmap);
    if (g_renderer.dest_dc) DeleteDC(g_renderer.dest_dc);
    if (g_renderer.mask_dc) DeleteDC(g_renderer.mask_dc);
    if (g_renderer.screen) ReleaseDC(NULL, g_renderer.screen);
    free(g_renderer.base);
    g_renderer.screen = NULL; g_renderer.dest_dc = NULL; g_renderer.mask_dc = NULL;
    g_renderer.dest_bitmap = NULL; g_renderer.mask_bitmap = NULL;
    g_renderer.old_dest = NULL; g_renderer.old_mask = NULL;
    g_renderer.pixels = NULL; g_renderer.mask = NULL; g_renderer.base = NULL;
}

static BOOL AllocateBuffers(int width, int height) {
    DestroyBuffers();
    g_renderer.width = width; g_renderer.height = height;
    g_renderer.screen = GetDC(NULL);
    if (!g_renderer.screen) return FALSE;
    g_renderer.dest_dc = CreateCompatibleDC(g_renderer.screen);
    g_renderer.mask_dc = CreateCompatibleDC(g_renderer.screen);
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width; bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_renderer.dest_bitmap = CreateDIBSection(g_renderer.screen, &bi, DIB_RGB_COLORS,
                                               (void **)&g_renderer.pixels, NULL, 0);
    g_renderer.mask_bitmap = CreateDIBSection(g_renderer.screen, &bi, DIB_RGB_COLORS,
                                               (void **)&g_renderer.mask, NULL, 0);
    if (!g_renderer.dest_dc || !g_renderer.mask_dc || !g_renderer.dest_bitmap ||
        !g_renderer.mask_bitmap || !g_renderer.pixels || !g_renderer.mask) {
        DestroyBuffers(); return FALSE;
    }
    g_renderer.old_dest = SelectObject(g_renderer.dest_dc, g_renderer.dest_bitmap);
    g_renderer.old_mask = SelectObject(g_renderer.mask_dc, g_renderer.mask_bitmap);
    g_renderer.base = (uint32_t *)calloc((size_t)width * height, sizeof(uint32_t));
    if (!g_renderer.base) { DestroyBuffers(); return FALSE; }
    return TRUE;
}

static void BuildGlassBase(const Settings *s) {
    int w = g_renderer.width, h = g_renderer.height;
    int scale = (int)g_renderer.dpi;
    int inset = MulDiv(8, scale, 96);
    int radius = MulDiv(22, scale, 96);
    int alpha = 255 * (100 - s->background_transparency) / 100;
    int br = GetRValue(s->background_color), bg = GetGValue(s->background_color), bb = GetBValue(s->background_color);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            BOOL inside = InRoundedRect(x, y, inset, inset, w - inset, h - inset, radius);
            if (inside) {
                int content = h - inset * 2;
                int light = 108 - 16 * (y - inset) / (content > 1 ? content - 1 : 1);
                uint32_t px = PackPixel(br * light / 100, bg * light / 100, bb * light / 100, alpha);
                BOOL inner = InRoundedRect(x, y, inset + 1, inset + 1, w - inset - 1, h - inset - 1, radius - 1);
                if (!inner) px = BlendPixel(px, 255, 255, 255, 42 * alpha / 255);
                g_renderer.base[y * w + x] = px;
            } else if (alpha > 0) {
                for (int spread = 1; spread <= inset; ++spread) {
                    if (InRoundedRect(x, y, inset - spread, inset - spread,
                                      w - inset + spread, h - inset + spread, radius + spread)) {
                        g_renderer.base[y * w + x] = PackPixel(0, 0, 0, (inset - spread + 1) * 24 / (inset + 1));
                        break;
                    }
                }
            }
        }
    }
}

static void BuildRingBase(const Settings *s) {
    int w = g_renderer.width, h = g_renderer.height;
    double cx = w / 2.0, cy = h / 2.0;
    double outer = (w < h ? w : h) / 2.0 - MulDiv(7, (int)g_renderer.dpi, 96);
    double thickness = MulDiv(7, (int)g_renderer.dpi, 96);
    int alpha = 255 * (100 - s->background_transparency) / 100;
    int br = GetRValue(s->background_color), bg = GetGValue(s->background_color), bb = GetBValue(s->background_color);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double distance = sqrt(dx * dx + dy * dy);
            if (distance <= outer && distance >= outer - thickness) {
                g_renderer.base[y * w + x] = PackPixel(170, 180, 197, 80);
            } else if (distance < outer - thickness - 2.0) {
                g_renderer.base[y * w + x] = PackPixel(br, bg, bb, alpha);
            }
        }
    }
}

static void BuildBase(const Settings *s) {
    ZeroMemory(g_renderer.base, (SIZE_T)g_renderer.width * g_renderer.height * sizeof(uint32_t));
    if (s->theme == THEME_GLASS) BuildGlassBase(s);
    else if (s->theme == THEME_RING) BuildRingBase(s);
}

static void DrawLineProgress(double fraction, COLORREF color) {
    int w = g_renderer.width, h = g_renderer.height;
    int margin = MulDiv(22, (int)g_renderer.dpi, 96);
    int line_h = MulDiv(3, (int)g_renderer.dpi, 96);
    if (line_h < 2) line_h = 2;
    int y0 = h - MulDiv(15, (int)g_renderer.dpi, 96);
    int x0 = margin, x1 = w - margin;
    int fill = x0 + (int)((x1 - x0) * fraction);
    int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    for (int y = y0; y < y0 + line_h && y < h; ++y) {
        for (int x = x0; x < x1; ++x) {
            int a = x < fill ? 235 : 50;
            g_renderer.pixels[y * w + x] = BlendPixel(g_renderer.pixels[y * w + x], cr, cg, cb, a);
        }
    }
}

static void DrawRingProgress(double fraction, COLORREF color) {
    int w = g_renderer.width, h = g_renderer.height;
    double cx = w / 2.0, cy = h / 2.0;
    double outer = (w < h ? w : h) / 2.0 - MulDiv(7, (int)g_renderer.dpi, 96);
    double thickness = MulDiv(7, (int)g_renderer.dpi, 96);
    double limit = fraction * 6.28318530717958647692;
    int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double distance = sqrt(dx * dx + dy * dy);
            if (distance <= outer && distance >= outer - thickness) {
                double angle = atan2(dy, dx) + 1.57079632679489661923;
                if (angle < 0.0) angle += 6.28318530717958647692;
                if (angle <= limit) g_renderer.pixels[y * w + x] = BlendPixel(g_renderer.pixels[y * w + x], cr, cg, cb, 245);
            }
        }
    }
}

static void DrawTextMask(LPCWSTR text) {
    ZeroMemory(g_renderer.mask, (SIZE_T)g_renderer.width * g_renderer.height * sizeof(uint32_t));
    SetBkMode(g_renderer.mask_dc, TRANSPARENT);
    SetTextColor(g_renderer.mask_dc, RGB(255, 255, 255));
    HFONT old_font = (HFONT)SelectObject(g_renderer.mask_dc, g_renderer.font);
    RECT rect = {0, 0, g_renderer.width, g_renderer.height};
    if (g_renderer.width == g_renderer.height) {
        int inset = MulDiv(16, (int)g_renderer.dpi, 96);
        rect.left += inset; rect.right -= inset;
    }
    DrawTextW(g_renderer.mask_dc, text, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(g_renderer.mask_dc, old_font);
}

BOOL RendererApplySettings(HWND window, const Settings *s) {
    UINT dpi = GetDpiForWindow(window);
    if (!dpi) dpi = 96;
    if (g_renderer.font) { DeleteObject(g_renderer.font); g_renderer.font = NULL; }
    int height = -MulDiv(s->font_size, (int)dpi, 72);
    int weight = s->theme == THEME_MINIMAL ? FW_SEMIBOLD : FW_MEDIUM;
    g_renderer.font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (!g_renderer.font) return FALSE;

    HDC measure = GetDC(window);
    HFONT old = (HFONT)SelectObject(measure, g_renderer.font);
    SIZE text_size = {0};
    GetTextExtentPoint32W(measure, L"000:00", 6, &text_size);
    SelectObject(measure, old); ReleaseDC(window, measure);

    int width, window_height;
    if (s->theme == THEME_RING) {
        int min_diameter = MulDiv(162, (int)dpi, 96);
        int diameter = text_size.cx + MulDiv(58, (int)dpi, 96);
        if (diameter < min_diameter) diameter = min_diameter;
        width = window_height = diameter;
    } else if (s->theme == THEME_MINIMAL) {
        width = text_size.cx + MulDiv(42, (int)dpi, 96);
        window_height = text_size.cy + MulDiv(38, (int)dpi, 96);
    } else {
        width = text_size.cx + MulDiv(70, (int)dpi, 96);
        window_height = text_size.cy + MulDiv(54, (int)dpi, 96);
    }
    g_renderer.dpi = dpi;
    if (!AllocateBuffers(width, window_height)) return FALSE;
    BuildBase(s);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, width, window_height,
                 SWP_NOMOVE | SWP_NOACTIVATE);
    return TRUE;
}

BOOL RendererInitialize(HWND window, const Settings *settings) {
    ZeroMemory(&g_renderer, sizeof(g_renderer));
    return RendererApplySettings(window, settings);
}

void RendererDraw(HWND window, const Settings *s, ULONGLONG shown_seconds,
                  ULONGLONG remaining_ms, ULONGLONG duration_ms,
                  BOOL expired, BOOL blink_white) {
    if (!g_renderer.base || !g_renderer.pixels) return;
    SIZE_T bytes = (SIZE_T)g_renderer.width * g_renderer.height * sizeof(uint32_t);
    memcpy(g_renderer.pixels, g_renderer.base, bytes);
    double fraction = duration_ms ? (double)remaining_ms / (double)duration_ms : 0.0;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    COLORREF progress = shown_seconds <= 60 ? s->warning_color : s->normal_color;
    if (s->theme == THEME_RING) DrawRingProgress(fraction, progress);
    else DrawLineProgress(fraction, progress);

    int minutes = (int)(shown_seconds / 60), seconds = (int)(shown_seconds % 60);
    WCHAR text[32]; wsprintfW(text, L"%02d:%02d", minutes, seconds);
    DrawTextMask(text);
    COLORREF fg = expired ? (blink_white ? RGB(255,255,255) : s->warning_color) : progress;
    int fr = GetRValue(fg), fg_green = GetGValue(fg), fb = GetBValue(fg);
    int count = g_renderer.width * g_renderer.height;
    for (int i = 0; i < count; ++i) {
        uint32_t m = g_renderer.mask[i];
        int coverage = ((int)(m & 255) + (int)((m >> 8) & 255) + (int)((m >> 16) & 255)) / 3;
        if (coverage) g_renderer.pixels[i] = BlendPixel(g_renderer.pixels[i], fr, fg_green, fb, coverage);
    }

    RECT wr; GetWindowRect(window, &wr);
    POINT source = {0, 0}, position = {wr.left, wr.top};
    SIZE size = {g_renderer.width, g_renderer.height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, g_renderer.screen, &position, &size,
                        g_renderer.dest_dc, &source, 0, &blend, ULW_ALPHA);
}

void RendererShutdown(void) {
    DestroyBuffers();
    if (g_renderer.font) DeleteObject(g_renderer.font);
    ZeroMemory(&g_renderer, sizeof(g_renderer));
}
