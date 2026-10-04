#include "opengl_support.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <mutex>

#include "loguru/loguru.hpp"

namespace opengl_support {
    namespace {
        std::once_flag g_probe_once;
        bool g_available = false;

        void probe() {
            // The default format is the one main() set up for the voxel view
            // (OpenGL 3.3 core), so a successful probe means the 3D view works.
            QOpenGLContext context;
            context.setFormat(QSurfaceFormat::defaultFormat());
            if (!context.create()) {
                LOG_F(WARNING, "OpenGL context creation failed (Qt platform: %s)",
                      QGuiApplication::platformName().toStdString().c_str());
                return;
            }

            QOffscreenSurface surface;
            surface.setFormat(context.format());
            surface.create();
            if (!surface.isValid()) {
                LOG_F(WARNING, "OpenGL offscreen surface creation failed");
                return;
            }

            g_available = context.makeCurrent(&surface);
            if (g_available) {
                context.doneCurrent();
                LOG_F(INFO, "OpenGL context available (Qt platform: %s)",
                      QGuiApplication::platformName().toStdString().c_str());
            } else {
                LOG_F(WARNING, "OpenGL context could not be made current");
            }
        }
    }  // namespace

    bool available() {
        std::call_once(g_probe_once, probe);
        return g_available;
    }

}  // namespace opengl_support
