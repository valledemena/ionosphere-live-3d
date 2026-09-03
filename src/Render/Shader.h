#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    GLuint program = 0;

    bool load(const char* vertPath, const char* fragPath);
    bool loadFromSource(const char* vertSrc, const char* fragSrc);
    void use() const;
    void setMat4(const char* name, const glm::mat4& m) const;
    void setVec3(const char* name, const glm::vec3& v) const;
    void setVec2(const char* name, const glm::vec2& v) const;
    void setFloat(const char* name, float v) const;
    void setInt(const char* name, int v) const;
    void setBool(const char* name, bool v) const;

private:
    GLuint compile(GLenum type, const char* src);
};
