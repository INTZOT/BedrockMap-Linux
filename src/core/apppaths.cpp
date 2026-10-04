#include "apppaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <mutex>

#include "loguru/loguru.hpp"

namespace {

    QString cleanPath(const QString& path) { return path.isEmpty() ? QString() : QDir::cleanPath(path); }

    // Environment override, e.g. BEDROCKMAP_DATA_DIR. Empty when unset.
    QString envDir(const char* name) { return cleanPath(qEnvironmentVariable(name)); }

    QString homeDir() {
        const QString home = qEnvironmentVariable("HOME");
        if (!home.isEmpty()) return cleanPath(home);
        return QDir::homePath();
    }

    // XDG base directory: <env override> or <home>/<fallback relative path>.
    QString xdgDir(const char* envName, const QString& fallbackRelative) {
        const QString override = envDir(envName);
        if (!override.isEmpty()) return override;
        return QDir::cleanPath(homeDir() + QLatin1Char('/') + fallbackRelative);
    }

    QString xdgConfigHome() { return xdgDir("XDG_CONFIG_HOME", QStringLiteral(".config")); }
    QString xdgDataHome() { return xdgDir("XDG_DATA_HOME", QStringLiteral(".local/share")); }
    QString xdgStateHome() { return xdgDir("XDG_STATE_HOME", QStringLiteral(".local/state")); }

    // Directories that contain the released asset set (block_color.json,
    // biome_color.json, shaders/, translations/).
    QStringList buildDataRoots() {
        const QString exe = apppaths::executableDir();
        const QString cwd = cleanPath(QDir::currentPath());
        QStringList roots;
        const auto add = [&roots](const QString& root) {
            if (!root.isEmpty() && !roots.contains(root)) roots.push_back(root);
        };
        add(envDir("BEDROCKMAP_DATA_DIR"));
        add(exe);
        add(cwd);
#ifdef Q_OS_WIN
        // Portable Windows layout: everything lives next to the executable.
#else
        add(cleanPath(exe + QStringLiteral("/../share/BedrockMap")));
        add(QStringLiteral("/usr/local/share/BedrockMap"));
        add(QStringLiteral("/usr/share/BedrockMap"));
        add(cleanPath(xdgDataHome() + QStringLiteral("/BedrockMap")));
        add(cleanPath(exe + QStringLiteral("/..")));
        add(cleanPath(cwd + QStringLiteral("/..")));
#endif
        return roots;
    }

    // Source-tree root candidates for development builds. The executable is in
    // <repo>/build (or <repo>/build_rls), so the repository root is one level up.
    QStringList buildRepoTrees() {
        QStringList trees;
        const auto add = [&trees](const QString& root) {
            if (!root.isEmpty() && !trees.contains(root)) trees.push_back(root);
        };
        add(cleanPath(apppaths::executableDir() + QStringLiteral("/..")));
        add(cleanPath(QDir::currentPath() + QStringLiteral("/..")));
        return trees;
    }

    bool containsQmFiles(const QString& dir) {
        if (dir.isEmpty()) return false;
        QDir d(dir);
        if (!d.exists()) return false;
        return !d.entryList(QStringList{QStringLiteral("*.qm")}, QDir::Files).isEmpty();
    }

}  // namespace

namespace apppaths {

    QString executableDir() {
        static QString cached;
        static std::once_flag once;
        std::call_once(once, [] {
#ifdef Q_OS_LINUX
            // Available before a QCoreApplication exists, which matters for the
            // log directory that is set up at the very start of main().
            const QString proc = QFile::symLinkTarget(QStringLiteral("/proc/self/exe"));
            if (!proc.isEmpty()) {
                cached = QFileInfo(proc).absolutePath();
                return;
            }
#endif
            if (QCoreApplication::instance() != nullptr) {
                cached = QCoreApplication::applicationDirPath();
            } else {
                cached = QDir::currentPath();
            }
        });
        return cached;
    }

    QStringList dataRoots() {
        static QStringList cached;
        static std::once_flag once;
        std::call_once(once, [] { cached = buildDataRoots(); });
        return cached;
    }

    QString resolveDataFile(const QString& relative, const QString& devRelative) {
        for (const auto& root : dataRoots()) {
            const QString candidate = QDir(root).absoluteFilePath(relative);
            if (QFileInfo::exists(candidate)) return candidate;
        }
        if (!devRelative.isEmpty()) {
            for (const auto& tree : buildRepoTrees()) {
                const QString candidate = QDir(tree).absoluteFilePath(devRelative);
                if (QFileInfo::exists(candidate)) return candidate;
            }
        }
        return {};
    }

    QString dataFile(const QString& relative, const QString& devRelative) {
        const QString found = resolveDataFile(relative, devRelative);
        if (!found.isEmpty()) return found;
        return QDir(dataDir()).absoluteFilePath(relative);
    }

    const QString& dataDir() {
        static const QString cached = [] {
            for (const auto& root : dataRoots()) {
                if (QFileInfo::exists(QDir(root).absoluteFilePath(QStringLiteral("block_color.json")))) return root;
            }
            for (const auto& root : dataRoots()) {
                if (QFileInfo(root).isDir()) return root;
            }
#ifdef Q_OS_WIN
            return executableDir();
#else
            return cleanPath(xdgDataHome() + QStringLiteral("/BedrockMap"));
#endif
        }();
        return cached;
    }

    const QString& configFile() {
        static const QString cached = [] {
            const QString override = envDir("BEDROCKMAP_CONFIG_DIR");
            if (!override.isEmpty()) return cleanPath(override + QStringLiteral("/config.ini"));

            // Portable installations and development trees keep using the
            // configuration file that is already there.
            QStringList existing;
            existing << cleanPath(executableDir() + QStringLiteral("/config.ini"));
            existing << cleanPath(QDir::currentPath() + QStringLiteral("/config.ini"));
            for (const auto& tree : buildRepoTrees()) existing << cleanPath(tree + QStringLiteral("/config.ini"));
            for (const auto& path : existing) {
                if (QFileInfo::exists(path)) return path;
            }
#ifdef Q_OS_WIN
            return cleanPath(executableDir() + QStringLiteral("/config.ini"));
#else
            return cleanPath(xdgConfigHome() + QStringLiteral("/BedrockMap/config.ini"));
#endif
        }();
        return cached;
    }

    const QString& logDir() {
        static const QString cached = [] {
            const QString override = envDir("BEDROCKMAP_LOG_DIR");
#ifdef Q_OS_WIN
            const QString preferred = override.isEmpty() ? cleanPath(executableDir() + QStringLiteral("/logs")) : override;
#else
            const QString preferred =
                override.isEmpty() ? cleanPath(xdgStateHome() + QStringLiteral("/BedrockMap/logs")) : override;
#endif
            if (QDir().mkpath(preferred)) return preferred;
            // Read-only home or restricted environment: fall back to the working
            // directory, which is what older releases always used.
            const QString fallback = cleanPath(QDir::currentPath() + QStringLiteral("/logs"));
            if (QDir().mkpath(fallback)) {
                LOG_F(WARNING, "Cannot create log directory %s, using %s instead", preferred.toStdString().c_str(),
                      fallback.toStdString().c_str());
                return fallback;
            }
            return preferred;
        }();
        return cached;
    }

    QString translationsDir() {
        static QString cached;
        static std::once_flag once;
        std::call_once(once, [] {
            QStringList candidates;
            for (const auto& root : dataRoots()) {
                candidates << cleanPath(root + QStringLiteral("/translations"));
                // Development builds drop the .qm files straight into the CMake
                // binary directory, so the root itself has to be scanned too.
                candidates << root;
            }
            for (const auto& tree : buildRepoTrees()) {
                candidates << cleanPath(tree + QStringLiteral("/translations"));
                candidates << cleanPath(tree + QStringLiteral("/build"));
                candidates << cleanPath(tree + QStringLiteral("/build_rls"));
            }
            candidates.removeDuplicates();
            for (const auto& dir : candidates) {
                if (containsQmFiles(dir)) {
                    cached = dir;
                    return;
                }
            }
        });
        return cached;
    }

}  // namespace apppaths
