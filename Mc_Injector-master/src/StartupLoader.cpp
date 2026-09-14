#include "StartupLoader.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

StartupLoader::StartupLoader(QQmlApplicationEngine& engine,
                             std::function<void()> prepare, std::function<void()> ready)
    : m_engine(engine), m_prepare(std::move(prepare)), m_ready(std::move(ready)),
      m_component(&engine), m_incubator(*this)
{
    connect(&m_component, &QQmlComponent::statusChanged, this,
            [this] { componentReady(); });
}

StartupLoader::~StartupLoader()
{
    // The main QWindow must remain top-level (QObject parenting a window to
    // the engine is not a valid substitute for owning a top-level QWindow).
    m_engine.setIncubationController(nullptr);
    m_incubator.clear();
    delete m_main.data();
}

QQuickWindow* StartupLoader::mainWindow() const { return m_main.data(); }

void StartupLoader::start()
{
    m_clock.start();
    m_engine.setInitialProperties({{QStringLiteral("darkTheme"),
        QSettings().value(QStringLiteral("ui/darkTheme"), false)}});
    m_engine.load(QUrl(QStringLiteral("qrc:/qml/Startup.qml")));
    if (m_engine.rootObjects().isEmpty()) {
        fail(QStringLiteral("Could not load the startup window."));
        return;
    }
    m_splash = qobject_cast<QQuickWindow*>(m_engine.rootObjects().first());
    if (!m_splash) { fail(QStringLiteral("Invalid startup window.")); return; }
    m_engine.setIncubationController(m_splash->incubationController());
    connect(m_splash, &QQuickWindow::frameSwapped, this, [this] {
        qInfo() << "Startup first frame (ms):" << m_clock.elapsed();
        const QString captureDirectory = qEnvironmentVariable("MC_OVERLAY_STARTUP_CAPTURE_DIR");
        if (!captureDirectory.isEmpty())
            m_splash->grabWindow().save(QDir(captureDirectory).filePath(QStringLiteral("startup.png")));
        // Only begin expensive initialization after the loading view is visible.
        QTimer::singleShot(0, this, &StartupLoader::loadMain);
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
}

void StartupLoader::loadMain()
{
    if (m_failed || !m_splash) return;
    m_splash->setProperty("phaseText", QStringLiteral("Preparing your workspace"));
    m_prepare();
    m_component.loadUrl(QUrl(QStringLiteral("qrc:/qml/main.qml")), QQmlComponent::Asynchronous);
    componentReady();
}

void StartupLoader::componentReady()
{
    if (m_failed || m_creating) return;
    if (m_component.isError()) { fail(m_component.errorString()); return; }
    if (!m_component.isReady()) return;
    m_creating = true;
    m_splash->setProperty("phaseText", QStringLiteral("Loading your interface"));
    m_incubator.setInitialProperties({{QStringLiteral("startupReady"), false}});
    m_component.create(m_incubator);
}

void StartupLoader::Incubator::statusChanged(Status status)
{
    qInfo() << "Startup incubation status:" << status;
    // Do not mutate/destroy an incubator while Qt is inside its callback.
    if (status == Ready) QTimer::singleShot(0, &m_owner, &StartupLoader::showMain);
    else if (status == Error) {
        QStringList details;
        for (const auto& error : errors()) details.append(error.toString());
        QTimer::singleShot(0, &m_owner, [owner = &m_owner, message = details.join('\n')] {
            owner->fail(message);
        });
    }
}

void StartupLoader::showMain()
{
    qInfo() << "Startup main object created (ms):" << m_clock.elapsed();
    if (m_failed) return;
    m_main = qobject_cast<QQuickWindow*>(m_incubator.object());
    if (!m_main) { fail(QStringLiteral("Invalid main window.")); return; }
    // create(incubator) doesn't put it in engine.rootObjects(). Own it here,
    // without assigning any QWindow parent (which would prevent top-level exposure).
    QQmlEngine::setObjectOwnership(m_main, QQmlEngine::CppOwnership);
    m_engine.setIncubationController(m_main->incubationController());
    connect(m_main, &QQuickWindow::frameSwapped, this, [this] {
        qInfo() << "Startup workspace ready (ms):" << m_clock.elapsed();
        const QString captureDirectory = qEnvironmentVariable("MC_OVERLAY_STARTUP_CAPTURE_DIR");
        if (!captureDirectory.isEmpty())
            m_main->grabWindow().save(QDir(captureDirectory).filePath(QStringLiteral("main-window.png")));
        if (m_splash) m_splash->setProperty("completed", true);
        m_ready();
    }, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    m_main->setProperty("startupReady", true);
#ifdef Q_OS_WIN
    // Start-Process -WindowStyle Hidden supplies SW_HIDE for the first normal
    // native window (the frameless splash does not consume it on Windows).
    // In our explicit automated test only, restore exposure after that first
    // ShowWindow. Do not misdiagnose a test-launch hint as a QML/render failure.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test")) &&
        !IsWindowVisible(reinterpret_cast<HWND>(m_main->winId()))) {
        qInfo() << "Startup test: restoring native visibility suppressed by launch hint";
        m_main->hide();
        m_main->show();
    }
#endif
    qInfo() << "Startup main visibility:" << m_main->isVisible();
    QTimer::singleShot(1000, this, [this] {
        if (m_main) qInfo() << "Startup window diagnostics:" << m_main->geometry()
            << m_main->isExposed() << m_main->parent() << m_main->flags()
            << m_main->opacity() << m_main->visibility();
    });
}

void StartupLoader::fail(const QString& message)
{
    m_failed = true;
    qCritical().noquote() << "Startup failed:" << message;
    if (m_splash) {
        m_splash->setProperty("failed", true);
        m_splash->setProperty("phaseText", QStringLiteral("Unable to load the interface. Please check the application files."));
    } else QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(1); });
}
