#ifndef PRESENTATION_TIMER_CONTRAST_H
#define PRESENTATION_TIMER_CONTRAST_H
#include <stdint.h>
#include <math.h>

static inline double ContrastLinearChannel(unsigned channel) {
    double s = channel / 255.0;
    return s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}

/* 0x00RRGGBB screen, 0xAARRGGBB premultiplied timer panel. */
static inline double ContrastCompositedLuminance(uint32_t screen, uint32_t panel) {
    unsigned inv = 255 - (panel >> 24);
    unsigned r = ((panel >> 16) & 255) + (((screen >> 16) & 255) * inv + 127) / 255;
    unsigned g = ((panel >> 8) & 255) + (((screen >> 8) & 255) * inv + 127) / 255;
    unsigned b = (panel & 255) + ((screen & 255) * inv + 127) / 255;
    return 0.2126 * ContrastLinearChannel(r) +
           0.7152 * ContrastLinearChannel(g) +
           0.0722 * ContrastLinearChannel(b);
}

static inline int ContrastUseWhite(double luminance, int has_previous, int previous_white) {
    /* Equal black/white contrast at relative luminance ~0.179. Small dead
       band prevents flashing on a slide transition or an animated background. */
    if (!has_previous) return luminance < 0.179;
    return previous_white ? luminance < 0.190 : luminance < 0.168;
}

#endif
