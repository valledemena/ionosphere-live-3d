#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

static std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Shader::load(const char* vertPath, const char* fragPath) {
    std::string vertSrc = readFile(vertPath);
    std::string fragSrc = readFile(fragPath);
    if (vertSrc.empty() || fragSrc.empty()) return false;
    return loadFromSource(vertSrc.c_str(), fragSrc.c_str());
}

bool Shader::loadFromSource(const char* vertSrc, const char* fragSrc) {
    GLuint v = compile(GL_VERTEX_SHADER, vertSrc);
    GLuint f = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!v || !f) return false;

    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        fprintf(stderr, "Shader link error: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return program != 0;
}

GLuint Shader::compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader compile error (%s): %s\n",
                type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void Shader::use() const { glUseProgram(program); }
void Shader::setMat4(const char* n, const glm::mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(program, n), 1, GL_FALSE, glm::value_ptr(m));
}
void Shader::setVec3(const char* n, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(program, n), 1, glm::value_ptr(v));
}
void Shader::setVec2(const char* n, const glm::vec2& v) const {
    glUniform2fv(glGetUniformLocation(program, n), 1, glm::value_ptr(v));
}
void Shader::setFloat(const char* n, float v) const {
    glUniform1f(glGetUniformLocation(program, n), v);
}
void Shader::setInt(const char* n, int v) const {
    glUniform1i(glGetUniformLocation(program, n), v);
}
void Shader::setBool(const char* n, bool v) const {
    glUniform1i(glGetUniformLocation(program, n), v ? 1 : 0);
}
