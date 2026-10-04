#ifndef BEDROCKMAP_OPENGL_SUPPORT_H
#define BEDROCKMAP_OPENGL_SUPPORT_H

// Result of a one-time OpenGL capability probe.
//
// Qt cannot create an OpenGL context on every Linux setup: Wayland sessions that
// drive an NVIDIA GPU through EGL are a known case (the X11/XWayland backend
// works there), and virtual machines may have no GL driver at all. The 2D map,
// the NBT editor and every file operation work without a context, so the
// application only has to keep the 3D voxel view from failing silently.
namespace opengl_support {

    /// True when a 3D-capable OpenGL context can be created. The probe runs on the
    /// first call (including the failure reason in the log) and is cached.
    bool available();

}  // namespace opengl_support

#endif  // BEDROCKMAP_OPENGL_SUPPORT_H
