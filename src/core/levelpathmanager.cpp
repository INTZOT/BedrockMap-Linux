#include "levelpathmanager.h"

#include <qdir.h>
#include <qdiriterator.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qjsonobject.h>

#include <QStandardPaths>
#include <fstream>

#include "config.h"
#include "json/json.hpp"
#include "level_dat.h"
#include "loguru/loguru.hpp"

using json = nlohmann::json;

namespace {

    void scanWorldsInDir(const QString& worldsDir, bool modern, bool preview, std::vector<LevelPathInfo>& out) {
        QDir dir(worldsDir);
        LOG_F(INFO, "scanWorldsInDir: %s exists=%d", worldsDir.toStdString().c_str(), dir.exists());
        if (!dir.exists()) return;
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& sub : entries) {
            auto info = LevelPathManager::makeLevelInfo(sub.absoluteFilePath());
            info.modern = modern;
            info.preview = preview;
            out.push_back(info);
        }
    }

#ifndef _WIN32
    // BedrockBoot (a Linux launcher that runs the Windows GDK build through Proton)
    // keeps the game data inside its Wine/Proton prefix. The per-user layout there
    // is the ordinary desktop one:
    //   <prefix>/drive_c/users/<user>/AppData/Roaming/Minecraft Bedrock/Users/<id>/
    //       games/com.mojang/minecraftWorlds
    // which is exactly what scanModernPaths() walks, so only the "AppData" roots
    // have to be handed over as modern scan paths.
    QStringList bedrockBootAppDataDirs() {
        QStringList result;
        const QString configRoot = QDir::homePath() + QStringLiteral("/.config/RoundStudio/BedrockBoot2");
        if (!QDir(configRoot).exists()) return result;

        // Prefixes live at <configRoot>/<component>/<prefix type>/game_prefix. The
        // walk is deliberately depth bounded: a Wine prefix holds tens of thousands
        // of entries and must never be traversed recursively at startup.
        const QDir root(configRoot);
        const auto components = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& component : components) {
            const QDir componentDir(component.absoluteFilePath());
            const auto prefixTypes = componentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const auto& prefixType : prefixTypes) {
                const QDir usersDir(QDir(prefixType.absoluteFilePath()).absoluteFilePath(QStringLiteral("game_prefix/drive_c/users")));
                const auto users = usersDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const auto& user : users) {
                    const QString appData = QDir(user.absoluteFilePath()).absoluteFilePath(QStringLiteral("AppData/Roaming"));
                    const QDir dir(appData);
                    if (!dir.exists()) continue;
                    if (!dir.exists(QString::fromStdString(LevelPathManager::DIR_MINECRAFT_BEDROCK)) &&
                        !dir.exists(QString::fromStdString(LevelPathManager::DIR_MINECRAFT_BEDROCK_PREVIEW))) {
                        continue;
                    }
                    if (!result.contains(appData)) result.push_back(appData);
                }
            }
        }
        return result;
    }
#endif

}  // namespace

LevelPathInfo LevelPathManager::makeLevelInfo(const QString& dirPath) {
    LevelPathInfo info;
    info.path = dirPath.toStdString();

    QDir dir(dirPath);
    bool hasLevelDat = dir.exists("level.dat");
    bool hasDb = dir.exists("db");
    info.isValid = hasLevelDat && hasDb;

    // levelname.txt
    QFile nameFile(dirPath + "/levelname.txt");
    if (nameFile.open(QIODevice::ReadOnly)) {
        info.levelName = QString::fromUtf8(nameFile.readAll()).trimmed().toStdString();
        nameFile.close();
    }

    // version from level.dat clientversion
    if (hasLevelDat) {
        bl::level_dat dat;
        if (dat.load_from_file((dirPath + "/level.dat").toStdString())) {
            if (info.levelName.empty()) {
                info.levelName = dat.level_name();
            }
            info.version = dat.min_compat_version().to_string();
        } else {
            info.isValid = false;
        }
    }

    // total size + newest file mtime
    int64_t totalSize = 0;
    int64_t newestTime = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        totalSize += fi.size();
        int64_t mtime = fi.lastModified().toSecsSinceEpoch();
        if (mtime > newestTime) newestTime = mtime;
    }
    info.sizeBytes = totalSize;
    info.lastModified = newestTime;

    return info;
}

const std::string LevelPathManager::GAMES_REL_PATH = "games/com.mojang/minecraftWorlds";
const std::string LevelPathManager::PACKAGE_UWP = "Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::PACKAGE_WINDOWS_BETA = "Packages/Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe/LocalState";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK = "Minecraft Bedrock";
const std::string LevelPathManager::DIR_MINECRAFT_BEDROCK_PREVIEW = "Minecraft Bedrock Preview";

LevelPathManager::LevelPathManager() {
    // Keep the recent-levels cache next to the configuration file so a portable
    // installation stays self-contained.
    QFileInfo configInfo(constant::configFilePath());
    filePath_ = configInfo.absoluteDir().absoluteFilePath("cache.json").toStdString();
    loadHistory();
}

LevelPathManager::~LevelPathManager() { saveHistory(); }

void LevelPathManager::addRecentPath(const QString& path) {
    history_.removeAll(path);
    history_.prepend(path);
    while (history_.size() > MAX_HISTORY) {
        history_.removeLast();
    }
}

QStringList LevelPathManager::recentPaths() const { return history_; }

void LevelPathManager::saveHistory() {
    json root;
    auto& arr = root["history"];
    for (const auto& p : history_) {
        arr.push_back(p.toStdString());
    }

    std::ofstream out(filePath_);
    if (!out.is_open()) {
        LOG_F(WARNING, "LevelPathManager: cannot write cache to %s", filePath_.c_str());
        return;
    }
    out << root.dump(2);
}

void LevelPathManager::loadHistory() {
    std::ifstream in(filePath_);
    if (!in.is_open()) return;
    try {
        json root;
        in >> root;
        if (root.contains("history") && root["history"].is_array()) {
            for (const auto& item : root["history"]) {
                history_.append(QString::fromStdString(item.get<std::string>()));
            }
        }
    } catch (std::exception& e) {
        LOG_F(WARNING, "LevelPathManager: failed to parse cache: %s", e.what());
        history_.clear();
    }
}

#ifndef _WIN32
const QStringList& LevelPathManager::linuxWorldRoots() {
    static const QStringList roots = [] {
        const QString home = QDir::homePath();
        QStringList list;
        const auto add = [&list](const QString& path) {
            if (path.isEmpty()) return;
            const QString clean = QDir::cleanPath(path);
            if (!list.contains(clean)) list.push_back(clean);
        };
        const auto addFromEnv = [&add](const char* env, const QString& suffix) {
            const QString base = qEnvironmentVariable(env);
            if (!base.isEmpty()) add(base + suffix);
        };
        // mcpelauncher (distribution package or a self-built install)
        addFromEnv("MCPELAUNCHER_DATA_DIR", "/games/com.mojang/minecraftWorlds");
        addFromEnv("XDG_DATA_HOME", "/mcpelauncher/games/com.mojang/minecraftWorlds");
        add(home + "/.local/share/mcpelauncher/games/com.mojang/minecraftWorlds");
        // mcpelauncher Flatpak (io.mrarm.mcpelauncher)
        add(home + "/.var/app/io.mrarm.mcpelauncher/data/mcpelauncher/games/com.mojang/minecraftWorlds");
        // Waydroid: world data is inside the Android container either on shared
        // storage or in the app-private directory.
        add(home + "/.local/share/waydroid/data/media/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds");
        add("/var/lib/waydroid/data/media/0/Android/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds");
        add("/var/lib/waydroid/data/data/com.mojang.minecraftpe/files/games/com.mojang/minecraftWorlds");
        return list;
    }();
    return roots;
}
#endif

QString LevelPathManager::defaultScanPath() {
#ifdef _WIN32
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString worlds = QDir(localAppData + "/" + QString::fromStdString(PACKAGE_UWP))
                                   .absoluteFilePath(QString::fromStdString(GAMES_REL_PATH));
        if (QDir(worlds).exists()) return worlds;
    }
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) return appData;
#else
    for (const auto& root : linuxWorldRoots()) {
        if (QDir(root).exists()) return root;
    }
#endif
    return QDir::homePath();
}

void LevelPathManager::init() {
#ifdef _WIN32
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    QString appData = qEnvironmentVariable("APPDATA");
    if (localAppData.isEmpty() || appData.isEmpty()) return;

    scan_paths_.push_back({appData.toStdString(), {}, "Official", true, false});

    if (setting::current().SCAN_LEVI_PATH) {
        initLeviPath();
    }
#else
    // Linux: mcpelauncher/Waydroid containers are scanned directly by
    // scanNormalPaths(), while BedrockBoot uses the desktop layout inside its
    // Proton prefix, so it is registered as a modern scan path instead.
    for (const auto& appData : bedrockBootAppDataDirs()) {
        scan_paths_.push_back({appData.toStdString(), {}, "BedrockBoot", true, false});
    }
#endif
}

void LevelPathManager::initLeviPath() {
#ifdef _WIN32
    QFile file(QString(qEnvironmentVariable("APPDATA")) + "/LeviLauncher.exe/config.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;

    auto root = doc.object();
    auto baseRoot = root.value("base_root").toString();
    if (baseRoot.isEmpty()) return;

    QDir versionsDir(baseRoot + "/versions");
    if (!versionsDir.exists()) return;

    auto versions = versionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& ver : versions) {
        scan_paths_.push_back({QDir(baseRoot).absoluteFilePath("versions/" + ver).toStdString(), ver.toStdString(), "Levi", true, false});
    }
#endif
}

void LevelPathManager::scanNormalPaths() {
    discovered_levels_.clear();
#ifdef _WIN32
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) return;

    scanWorldsInDir(QDir(localAppData + "/" + QString::fromStdString(PACKAGE_UWP)).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH)),
                    false, false, discovered_levels_);
    scanWorldsInDir(
        QDir(localAppData + "/" + QString::fromStdString(PACKAGE_WINDOWS_BETA)).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH)),
        false, false, discovered_levels_);
#else
    for (const auto& root : linuxWorldRoots()) {
        scanWorldsInDir(root, false, false, discovered_levels_);
    }
#endif
}

void LevelPathManager::scanModernPaths() {
    struct DirInfo {
        QString name;
        bool preview;
    };
    static const DirInfo kDirs[] = {
        {QString::fromStdString(DIR_MINECRAFT_BEDROCK), false},
        {QString::fromStdString(DIR_MINECRAFT_BEDROCK_PREVIEW), true},
    };

    for (const auto& entry : scan_paths_) {
        if (!entry.modern) continue;
        QString basePath = QString::fromStdString(entry.path);

        for (const auto& di : kDirs) {
            QString gameDir = basePath + "/" + di.name;
            QDir dir(gameDir);
            if (!dir.exists()) {
                LOG_F(INFO, "scanModern: game dir not found %s", gameDir.toStdString().c_str());
                continue;
            }

            QString usersDir = gameDir + "/Users";
            QDir users(usersDir);
            if (!users.exists()) {
                LOG_F(INFO, "scanModern: User dir not found %s", usersDir.toStdString().c_str());
                continue;
            }

            const auto userDirs = users.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const auto& user : userDirs) {
                QString worldsDir = QDir(user.absoluteFilePath()).absoluteFilePath(QString::fromStdString(GAMES_REL_PATH));
                QDir worlds(worldsDir);
                if (!worlds.exists()) {
                    LOG_F(INFO, "scanModern: worlds dir not found for user %s", user.fileName().toStdString().c_str());
                    continue;
                }

                const auto worldEntries = worlds.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const auto& world : worldEntries) {
                    auto info = makeLevelInfo(world.absoluteFilePath());
                    info.modern = true;
                    info.preview = di.preview;
                    discovered_levels_.push_back(info);
                }
            }
        }
    }
}

void LevelPathManager::dumpPaths() {
    LOG_F(INFO, "=== Scan paths ===");
    for (const auto& e : scan_paths_) {
        LOG_F(INFO, "  [%s] tag=%s modern=%d version=%s", e.path.c_str(), e.tag.c_str(), e.modern, e.version.c_str());
    }
    LOG_F(INFO, "=== Discovered levels ===");
    for (const auto& l : discovered_levels_) {
        LOG_F(INFO, "  [%s] valid=%d modern=%d preview=%d ver=%s name=%s", l.path.c_str(), l.isValid, l.modern, l.preview,
              l.version.c_str(), l.levelName.c_str());
    }
}
