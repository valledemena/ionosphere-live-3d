#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "App.h"
#include <cstdio>

int main() {
    App app;
    if (!app.init(1280, 720, "Ionosphere Live 3D")) {
        fprintf(stderr, "Failed to initialize app\n");
        return 1;
    }
    app.run();
    app.shutdown();
    return 0;
}
