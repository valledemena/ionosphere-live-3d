#pragma once
#include <glad/glad.h>
#include <cstdio>

// M1: FBO con MSAA opcional. Con samples > 0 el render va a una textura
// multisample (GL_TEXTURE_2D_MULTISAMPLE, RGBA16F) + depth multisample;
// resolve() baja el resultado por glBlitFramebuffer a un FBO hermano
// single-sample. colorTexture SIEMPRE es la textura resuelta (filtrable)
// que leen el bloom y el composite: las pasadas 2 y 3 no cambian. Con
// samples == 0 el comportamiento es exactamente el de siempre y resolve()
// es no-op (coste cero en el path clasico).
class Framebuffer {
public:
    GLuint fbo = 0;               // FBO de render (MSAA si samples > 0)
    GLuint colorTexture = 0;      // textura single-sample resuelta (RGBA16F)
    GLuint depthRenderbuffer = 0;
    int width = 0, height = 0;
    int samples = 0;              // muestras MSAA realmente en uso

    bool init(int w, int h, int msaaSamples = 0) {
        width = w;
        height = h;
        samples = (msaaSamples > 0) ? msaaSamples : 0;
        if (samples > 0) {
            // Acotar al maximo del driver antes de pedir el formato.
            GLint maxSamples = 0;
            glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
            if (maxSamples > 0 && samples > maxSamples) samples = maxSamples;
            if (samples > 0 && initMsaa(w, h))
                return true;
            static bool msaaWarned = false;  // Release-prep: log-una-vez
            if (!msaaWarned) {
                msaaWarned = true;
                fprintf(stderr, "Framebuffer: MSAA %dx no soportado, cayendo a 0x\n", msaaSamples);
            }
            destroy();   // limpia los ids parciales y los deja a 0
        }
        return initSingle(w, h);
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);
    }

    void unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // M1: MSAA -> colorTexture (single-sample). No-op con samples == 0.
    // Restaura el binding a 0 al terminar, igual que unbind().
    void resolve() const {
        if (samples <= 0 || !fbo || !resolveFbo) return;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy() {
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (resolveFbo) { glDeleteFramebuffers(1, &resolveFbo); resolveFbo = 0; }
        if (colorTexture) { glDeleteTextures(1, &colorTexture); colorTexture = 0; }
        if (msaaColorTexture) { glDeleteTextures(1, &msaaColorTexture); msaaColorTexture = 0; }
        if (depthRenderbuffer) { glDeleteRenderbuffers(1, &depthRenderbuffer); depthRenderbuffer = 0; }
        samples = 0;
    }

private:
    GLuint resolveFbo = 0;        // FBO hermano single-sample (solo MSAA)
    GLuint msaaColorTexture = 0;  // textura multisample (solo MSAA)

    // Path clasico (samples == 0): identico al Framebuffer pre-M1.
    bool initSingle(int w, int h) {
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

    // Path MSAA: fbo renderiza a multisample; resolveFbo solo recibe el blit
    // de color (no necesita depth) y expone el resultado en colorTexture.
    bool initMsaa(int w, int h) {
        // Resolve FBO (single-sample, RGBA16F filtrable).
        glGenFramebuffers(1, &resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

        // FBO de render MSAA.
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &msaaColorTexture);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaColorTexture);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA16F, w, h, GL_FALSE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msaaColorTexture, 0);

        glGenRenderbuffers(1, &depthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRenderbuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "Framebuffer (MSAA %dx) incomplete\n", samples);
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }
};
