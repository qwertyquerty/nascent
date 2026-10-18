#include "gse/render/FrameCapture.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <cstring>

namespace nascent {
    std::optional<CapturedFrame> capture_displayed_frame() {
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);

        if (viewport[2] <= 0 || viewport[3] <= 0) {
            return std::nullopt;
        }

        CapturedFrame frame;
        frame.width = viewport[2];
        frame.height = viewport[3];
        frame.pixels.resize((size_t)frame.width * frame.height * 4);

        while (glGetError() != GL_NO_ERROR) {
        }

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_FRONT);
        glReadPixels(viewport[0], viewport[1], frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE, frame.pixels.data());
        glReadBuffer(GL_BACK);

        if (glGetError() != GL_NO_ERROR) {
            return std::nullopt;
        }

        size_t stride = (size_t)frame.width * 4;
        std::vector<uint8_t> row(stride);

        for (int32_t y = 0; y < frame.height / 2; y++) {
            uint8_t* top = frame.pixels.data() + (size_t)y * stride;
            uint8_t* bottom = frame.pixels.data() + (size_t)(frame.height - 1 - y) * stride;
            std::memcpy(row.data(), top, stride);
            std::memcpy(top, bottom, stride);
            std::memcpy(bottom, row.data(), stride);
        }

        for (size_t index = 3; index < frame.pixels.size(); index += 4) {
            frame.pixels[index] = 255;
        }

        return frame;
    }
}
