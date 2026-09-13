#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "App.h"
#include <cstdio>

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // Release-prep: lineas atomicas en log multihilo
    setvbuf(stderr, nullptr, _IOLBF, 0);
    App app;
    if (!app.init(1280, 720, "Ionosphere Live 3D")) {
        fprintf(stderr, "Failed to initialize app\n");
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
