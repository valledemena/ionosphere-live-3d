#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

struct TECGrid {
    int width = 0;
    int height = 0;
    std::vector<float> values;
    double timestamp = 0.0;
    bool valid = false;
};

class TecLayer {
public:
    TecLayer();
    ~TecLayer();

    void init();
    void updateFromGrid(const TECGrid& grid);
    void render(const glm::mat4& view, const glm::mat4& projection);
    void destroy();

    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    float getAltitude() const { return altitude_; }
    void setAltitude(float alt) { altitude_ = alt; }

private:
    void createMesh();
    void updateTexture();
    void createViridisColormap();

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint texture_ = 0;
    GLuint shader_ = 0;

    int indexCount_ = 0;
    bool visible_ = false;
    float altitude_ = 0.02f;

    TECGrid grid_;
    std::vector<unsigned char> textureData_;
    std::vector<float> colormap_;

    bool meshDirty_ = true;
    bool textureDirty_ = true;
};
