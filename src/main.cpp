#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "media/Preview.hpp"
#include "app/PerformanceLog.hpp"
#include "app/Editor.hpp"
#include "app/FileAccess.hpp"
#include "app/UiExercise.hpp"
#include "app/Theme.hpp"
#include "app/CropExercise.hpp"
#include "app/MuteExercise.hpp"
#ifdef Q_OS_LINUX
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

int main(int argc, char **argv) {
#ifdef Q_OS_LINUX
    // Qt's early qt.conf lookup uses the kernel's executable name (AT_EXECFN).
    // Re-exec the real file so PATH/relative/symlink launches agree everywhere.
    auto executableName=QFile::encodeName(QFileInfo("/proc/self/exe").canonicalFilePath());
    if(!executableName.isEmpty()&&QByteArray(argv[0])!=executableName) {
        argv[0]=executableName.data();
        ::execv(executableName.constData(),argv);
        qCritical()<<"Cannot start ClipCut from its real location:"<<std::strerror(errno);return 2;
    }
    const auto runtimeDirectory=QFileInfo(QFile::decodeName(executableName)).absolutePath();
    if(QDir(runtimeDirectory+"/lib").exists()&&QFile::exists(runtimeDirectory+"/gst-plugin-scanner")) {
        const auto previous=qgetenv("LD_LIBRARY_PATH");
        qputenv("LD_LIBRARY_PATH",QFile::encodeName(runtimeDirectory+"/lib")+(previous.isEmpty()?QByteArray():QByteArray(":")+previous));
        if(!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","wayland");
    }
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ClipCut"); app.setApplicationName("ClipCut"); app.setApplicationVersion("0.1.0");
    QQuickStyle::setStyle("Basic");
    QQuickWindow::setDefaultAlphaBuffer(true);
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    const QString localPlugins = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.deps/gstreamer");
    const QString bundledPlugins = QCoreApplication::applicationDirPath() + "/gstreamer";
    if (QDir(bundledPlugins).exists()) {
        qputenv("GST_PLUGIN_SYSTEM_PATH", "");
        qputenv("GST_PLUGIN_PATH", bundledPlugins.toUtf8());
        const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(cache);
        qputenv("GST_REGISTRY", (cache + "/gstreamer-registry.bin").toUtf8());
#ifdef Q_OS_WIN
        qputenv("GST_PLUGIN_SCANNER", (QCoreApplication::applicationDirPath() + "/gst-plugin-scanner.exe").toUtf8());
#else
        qputenv("GST_PLUGIN_SCANNER", QFile::encodeName(QCoreApplication::applicationDirPath() + "/gst-plugin-scanner"));
#endif
    } else if (QFile::exists(localPlugins)) {
        auto old = qgetenv("GST_PLUGIN_PATH"); qputenv("GST_PLUGIN_PATH", localPlugins.toUtf8() + (old.isEmpty() ? QByteArray() : QByteArray(":") + old));
    }
    gst_init(nullptr, nullptr);
    QCommandLineParser parser; parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"input", "Open a recording", "path"});
    parser.addOption({"smoke-seconds", "Autoplay and exit after N seconds", "seconds"});
    parser.addOption({"screenshot", "Save a preview screenshot before exit", "path"});
    parser.addOption({"profile", "Write structured performance events and samples (JSONL)", "path"});
    parser.addOption({"theme", "Select graphite, dark-rose, or import a JSON theme", "preset-or-file"});
    parser.addOption({"exercise-preview", "Exercise rapid seeks and audio routing during a smoke run"});
    parser.addOption({"exercise-audio", "Solo each of the first three audio tracks in one-second intervals"});
    parser.addOption({"exercise-editor", "Exercise crop, edit shortcuts, palette, undo, and export; exit with test status", "output-directory"});
    parser.addOption({"exercise-files", "Exercise file browser filtering and keyboard navigation; exit with test status", "directory"});
    parser.addOption({"exercise-theme", "Exercise Appearance and verify rendered background/text/video alpha", "directory"});
    parser.addOption({"exercise-crop", "Exercise precise crop dragging, clamping and gesture undo", "directory"});
    parser.addOption({"exercise-mutes", "Exercise Ctrl-drag regions, track mute editing, preview and export", "directory"});
    parser.addPositionalArgument("clip","Recording to open (also supports dropping a file onto the Windows executable/shortcut).","[clip]");
    parser.process(app);
    const auto positional=parser.positionalArguments();
    if(positional.size()>1||(parser.isSet("input")&&!positional.isEmpty())){qCritical()<<"Provide one clip, either as a positional path or with --input.";return 2;}
    const auto input=parser.isSet("input")?parser.value("input"):positional.value(0);
    Preview preview;
    Editor editor(&preview);
    if (!preview.sink()) { qCritical().noquote() << preview.error(); return 2; }
    PerformanceLog profiler(parser.value("profile"), &preview, nullptr);
    qmlRegisterType<BrowserFilter>("ClipCut",1,0,"BrowserFilter");
    FileAccess files;
    Theme appTheme;
    if(parser.isSet("theme")) {
        const auto value=parser.value("theme");
        if(value=="graphite"||value=="dark-rose")appTheme.setPreset(value);
        else {const auto error=appTheme.importTheme(QUrl::fromLocalFile(QFileInfo(value).absoluteFilePath()));if(!error.isEmpty()){qCritical().noquote()<<error;return 2;}}
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appTheme",&appTheme);
    engine.rootContext()->setContextProperty("files",&files);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("profiler", &profiler);
    QObject::connect(&editor, &Editor::logEvent, &profiler, &PerformanceLog::event);
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&preview,&Preview::stop);
    engine.rootContext()->setContextProperty("preview", &preview);
#ifdef Q_OS_WIN
    engine.rootContext()->setContextProperty("videoComponent", "import QtQuick; import org.freedesktop.gstreamer.Qt6D3D11VideoItem 1.0; GstD3D11Qt6VideoItem {}");
#else
    engine.rootContext()->setContextProperty("videoComponent", "import QtQuick; import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0; GstGLQt6VideoItem {}");
#endif
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 3;
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    profiler.observeWindow(window);
    if(parser.isSet("exercise-files"))exerciseFiles(window,parser.value("exercise-files"),&profiler);
    if(parser.isSet("exercise-theme"))exerciseTheme(window,&appTheme,&preview,parser.value("exercise-theme"),&profiler,!input.isEmpty());
    if(parser.isSet("exercise-crop"))exerciseCrop(window,&editor,&preview,parser.value("exercise-crop"),&profiler);
    if(parser.isSet("exercise-mutes")){exerciseMutes(window,&editor,&preview,parser.value("exercise-mutes"),&profiler);QTimer::singleShot(120000,&app,[&]{app.exit(43);});}
    if(parser.isSet("exercise-editor")) { exerciseEditor(window,&editor,&preview,parser.value("exercise-editor"));QTimer::singleShot(180000,&app,[&]{app.exit(22);}); }
    if(!input.isEmpty()) {
        const auto url=QUrl(input);
        editor.open(url.isLocalFile()?url:QUrl::fromLocalFile(QFileInfo(input).absoluteFilePath()));
    }
    if (parser.isSet("smoke-seconds")) {
        QObject::connect(&preview, &Preview::durationChanged, &app, [&] { if(preview.duration()>0)preview.play(true); });
        QObject::connect(&preview, &Preview::errorChanged, &app, [&] { if (!preview.error().isEmpty()) app.exit(4); });
        QTimer::singleShot(parser.value("smoke-seconds").toInt() * 1000, &app, [&] {
            if (parser.isSet("screenshot")) window->grabWindow().save(parser.value("screenshot"));
            qInfo() << "SMOKE" << preview.position() << preview.presented() << preview.duration();
            app.exit(preview.duration() > 0 && preview.presented() > 0 && preview.error().isEmpty() ? 0 : 5);
        });
    }
    if (parser.isSet("exercise-preview")) {
        auto exercise = new QTimer(&app);
        QObject::connect(exercise, &QTimer::timeout, &app, [&, step = 0]() mutable {
            if (preview.duration() <= 0) return;
            if (step == 0) preview.play(false);
            if (step < 24) preview.seek(std::fmod(step * 0.731, std::max(0.2, preview.duration() - 0.5)), false);
            if (step == 24) { preview.seek(2.133, true); preview.route(1, {0, 1, 2}); }
            if (step == 32) { preview.play(true); preview.route(-1, {0, 1, 2}); exercise->stop(); }
            ++step;
        });
        QTimer::singleShot(1500, exercise, [exercise] { exercise->start(60); });
    }
    if(parser.isSet("exercise-audio")) {
        QObject::connect(&preview,&Preview::durationChanged,&app,[&] {
            if(preview.duration()<=0)return;
            preview.route(0,{0,1,2});
            QTimer::singleShot(1000,&preview,[&]{preview.route(1,{0,1,2});});
            QTimer::singleShot(2000,&preview,[&]{preview.route(2,{0,1,2});});
            QTimer::singleShot(3000,&preview,[&]{preview.route(-1,{0,2});});
        });
    }
    return app.exec();
}
