#include <qvector.h>

#include <QApplication>
#include <QCache>
#include <QChar>
#include <QCoreApplication>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTranslator>
#include <chrono>
#include <string>

#include "apppaths.h"
#include "config.h"
#include "crashhandler.h"
#include "loguru/loguru.hpp"
#include "mainwindow.h"
#include "opengl_support.h"
#include "resourcemanager.h"

void setupLog(int argc, char* argv[]) {
    // Logs live in the user state directory on Linux (<XDG_STATE_HOME>/BedrockMap/logs)
    // and next to the executable on Windows, so the application can be started from
    // any working directory (and from a read-only install location).
    const QString log_dir = apppaths::logDir();
    const auto p1 = std::chrono::system_clock::now();
    const auto log_file =
        (log_dir + "/" + QString::number(std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count()) + ".log")
            .toStdString();
    loguru::g_preamble_date = false;
    loguru::g_preamble_thread = false;
    loguru::g_colorlogtostderr = true;
    loguru::init(argc, argv);
    loguru::add_file(log_file.c_str(), loguru::Truncate, loguru::Verbosity_INFO);
}

void setupTheme(QApplication& a) {
    auto* hints = a.styleHints();
    if (setting::current().COLOR_THEME == "dark") {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (setting::current().COLOR_THEME == "light") {
        hints->setColorScheme(Qt::ColorScheme::Light);
    } else {
        hints->setColorScheme(Qt::ColorScheme::Unknown);
    }
}

// Default UI font. The original Windows default (Microsoft YaHei) does not exist
// on Linux, so fall back to the first installed Chinese-capable family and let Qt
// pick the platform default when none is found.
QString defaultFontFamily() {
#ifdef _WIN32
    return QStringLiteral("Microsoft YaHei");
#else
    static const char* const kCandidates[] = {
        "Noto Sans CJK SC", "Source Han Sans SC", "Source Han Sans CN", "Noto Sans SC",
        "WenQuanYi Zen Hei", "WenQuanYi Micro Hei", "Droid Sans Fallback", "DejaVu Sans",
    };
    const QStringList installed = QFontDatabase::families();
    for (const char* candidate : kCandidates) {
        const QString family = QString::fromUtf8(candidate);
        if (installed.contains(family)) return family;
    }
    return {};
#endif
}

void setupFont(QApplication& a) {
    auto id = QFontDatabase::addApplicationFont(":/res/fonts/JetBrainsMono-Regular.ttf");
    if (id == -1) {
        LOG_F(WARNING, "Can not load font");
    }
    QFont font;
    auto sz = setting::current().FONT_SIZE > 0 ? setting::current().FONT_SIZE : 10;
    auto family = !setting::current().FONT_FAMILY.isEmpty() ? setting::current().FONT_FAMILY : defaultFontFamily();
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setPointSize(sz);
    if (!family.isEmpty()) font.setFamily(family);
    QApplication::setFont(font);
}

// empty setting -> auto-detect from the system locale (Chinese -> zh_CN, otherwise en)
QString resolveLanguage() {
    auto s = setting::current();
    if (!s.LANGUAGE.isEmpty()) return s.LANGUAGE;
    s.LANGUAGE = QLocale::system().language() == QLocale::Chinese ? QString("zh_CN") : QString("en");
    setting::apply(s);
    return s.LANGUAGE;
}

int main(int argc, char* argv[]) {
    setupLog(argc, argv);
    LOG_F(INFO, "Start %s", constant::VERSION_STRING().toStdString().c_str());
    crashhandler::install();

    // QOpenGLWidget is a native child window on Windows. The first time it is shown
    // inside an already-visible top-level window whose pixel format does not match,
    // Qt has to destroy and recreate the top-level HWND — visible as the main window
    // closing and reopening. Pre-set the default surface format (matching VoxelWidget)
    // so every top-level window is created with the right format from the start.
    QSurfaceFormat gl_format;
    gl_format.setVersion(3, 3);
    gl_format.setProfile(QSurfaceFormat::CoreProfile);
    gl_format.setDepthBufferSize(24);
    gl_format.setStencilBufferSize(8);
    gl_format.setSamples(8);
    QSurfaceFormat::setDefaultFormat(gl_format);

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    // Wayland matches the window to the desktop entry of this name, so the icon
    // and the task-bar grouping work like a native application.
    QApplication::setDesktopFileName(QStringLiteral("BedrockMap"));
    QApplication::setApplicationName(QStringLiteral("BedrockMap"));
    QApplication::setApplicationVersion(constant::SOFTWARE_VERSION.toString());
    setting::init();
    constant::initColorTable();
    initResources();
    setupTheme(a);
    setupFont(a);
    TranslatorMgr::init();
    TranslatorMgr::setupTranslation(a, resolveLanguage());

    // Probe OpenGL once at startup so the reason ends up in the log. The 3D voxel
    // view warns the user when it is unavailable (see opengl_support.h); the rest
    // of the application works either way.
    opengl_support::available();

    MainWindow w;
    w.setWindowTitle(constant::VERSION_STRING());
    w.show();

    // Desktop/file-manager integration: "BedrockMap <world dir|.mcstructure|.nbt>"
    // opens the path directly. loguru consumes its own flags, so only positional
    // arguments are inspected.
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        w.openExternalPath(arguments.at(i));
    }
    return QApplication::exec();
}
