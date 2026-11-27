#include "../src/contrast.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    double dark = ContrastCompositedLuminance(0x00000000, 0);
    double light = ContrastCompositedLuminance(0x00ffffff, 0);
    assert(dark == 0.);
    assert(fabs(light - 1.) < 1e-12);
    assert(ContrastUseWhite(dark, 0, 0));
    assert(!ContrastUseWhite(light, 0, 1));
    assert(ContrastUseWhite(dark, 1, 0));
    assert(!ContrastUseWhite(light, 1, 1));
    assert(ContrastUseWhite(0.179, 1, 1));
    assert(!ContrastUseWhite(0.179, 1, 0));
    assert(!ContrastUseWhite(0.191, 1, 1));
    assert(ContrastUseWhite(0.167, 1, 0));
    assert(ContrastCompositedLuminance(0xffffff, 0xff000000) == dark);
    assert(fabs(ContrastCompositedLuminance(0, 0xffffffff) - light) < 1e-12);
    double half = ContrastCompositedLuminance(0xffffff, 0x80000000);
    assert(half > 0.2 && half < 0.22);
    assert(fabs(ContrastCompositedLuminance(0x00ff0000, 0) - .2126) < 1e-12);
    puts("Contrast tests passed");
}
