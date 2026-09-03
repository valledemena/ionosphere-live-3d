#pragma once
#include <glad/glad.h>
#include <cstdio>

class Framebuffer {
public:
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    int width = 0, height = 0;

    bool init(int w, int h) {
        width = w;
        height = h;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        glGenRenderbuffers(1, &depthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "Framebuffer incomplete\n");
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy() {
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (colorTexture) glDeleteTextures(1, &colorTexture);
        if (depthRenderbuffer) glDeleteRenderbuffers(1, &depthRenderbuffer);
    }
};
