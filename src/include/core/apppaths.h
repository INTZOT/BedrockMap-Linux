#ifndef BEDROCKMAP_APPPATHS_H
#define BEDROCKMAP_APPPATHS_H

#include <QString>
#include <QStringList>

// Resolves the runtime locations of configuration, logs and the read-only
// assets shipped with the application (block/biome color tables, shaders,
// compiled translations).
//
// Linux follows the XDG base directory specification so the application can be
// installed system-wide (binary in /usr/bin, assets in /usr/share/BedrockMap)
// while user state still lands in ~/.config and ~/.local/state. Windows keeps
// the original portable layout (files next to the executable).
//
// Every lookup honours an environment override first, which makes portable and
// development trees easy to redirect:
//   BEDROCKMAP_CONFIG_DIR, BEDROCKMAP_DATA_DIR, BEDROCKMAP_LOG_DIR
namespace apppaths {

    // Directory of the running executable. Works before QApplication exists
    // (reads /proc/self/exe on Linux) because the log directory is needed first.
    [[nodiscard]] QString executableDir();

    // Ordered directories that may contain the deployed assets. Directories do
    // not have to exist; resolveDataFile() skips the missing ones.
    [[nodiscard]] QStringList dataRoots();

    // First candidate holding 'relative', or an empty string when none does.
    // 'devRelative' is consulted in the source tree (build/../<devRelative>)
    // so development builds keep working from the CMake binary directory.
    [[nodiscard]] QString resolveDataFile(const QString& relative, const QString& devRelative = QString());

    // resolveDataFile() with a fallback inside dataDir() so callers always get a
    // usable path (also used for "file not found" diagnostics).
    [[nodiscard]] QString dataFile(const QString& relative, const QString& devRelative = QString());

    // Directory holding the assets that were actually found (or the preferred
    // XDG location when the installation is incomplete).
    [[nodiscard]] const QString& dataDir();

    // config.ini used by setting::load()/save(). An existing file next to the
    // executable, in the working directory or one level above it (development
    // build directory) is reused as a portable configuration; otherwise this is
    // <XDG_CONFIG_HOME>/BedrockMap/config.ini, created on the first save.
    [[nodiscard]] const QString& configFile();

    // Directory for run/crash logs. Created on demand, defaults to
    // <XDG_STATE_HOME>/BedrockMap/logs.
    [[nodiscard]] const QString& logDir();

    // First directory containing at least one *.qm file; empty when the
    // application was built without translations.
    [[nodiscard]] QString translationsDir();

}  // namespace apppaths

#endif  // BEDROCKMAP_APPPATHS_H
