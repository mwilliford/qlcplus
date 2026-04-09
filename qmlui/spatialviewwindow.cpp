/*
  Q Light Controller Plus
  spatialviewwindow.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include <QWidget>
#include <QHBoxLayout>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QQuickWidget>
#include <QQmlContext>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>

#include "spatialviewwindow.h"
#include "spatialview.h"
#include "spatialcontroller.h"
#include <cmath>
#include "bgfxrenderer.h"
#include <QPainter>
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

// ---------------------------------------------------------------------------
// SpatialViewWindow — defined entirely in this .cpp to avoid pulling <QWidget>
// into the header (which would trigger the QTransform name collision with
// Qt3DCore in the qmlui target).
// ---------------------------------------------------------------------------

class SpatialViewWindow : public QWidget
{
    Q_OBJECT
    Q_DISABLE_COPY(SpatialViewWindow)

    friend QImage grabSpatialViewWindow();
    friend void spatialViewSelectFixture(int32_t);
    friend void spatialViewAddSelectedFixture(int32_t);
    friend QJsonArray spatialViewGetFixtureScreenPositions();
    friend void spatialViewSetCamera(float, float, float);
    friend void spatialViewDrag(float, float, float, float, int);
    friend void spatialViewSetGizmoMode(int);

public:
    explicit SpatialViewWindow(Doc *doc)
        : QWidget(nullptr, Qt::Window)
        , m_doc(doc)
    {
        setWindowTitle(QStringLiteral("QLC+ Spatial View"));
        setAttribute(Qt::WA_DeleteOnClose, false);

        // --- bgfx viewport (QWindow → QWidget container) ---
        m_spatialView = new SpatialView(doc);
        QWidget *viewportContainer = QWidget::createWindowContainer(m_spatialView, this);
        viewportContainer->setMinimumSize(400, 300);
        viewportContainer->setFocusPolicy(Qt::StrongFocus);

        // --- Controller (C++ ↔ QML bridge) ---
        m_controller = new SpatialController(doc, m_spatialView, this);

        // When the user clicks a fixture in the viewport, update the controller
        m_spatialView->setSelectionCallback([this](int32_t fixtureId, int count) {
            m_controller->notifySelectionChanged(fixtureId, count);
        });

        // Gizmo mode: translate (0) or rotate (1)
        m_spatialView->setGizmoModeCallback(
            [this]() { return m_controller->gizmoMode(); },
            [this](int m) { m_controller->setGizmoMode(m); }
        );

        // Sync gizmo mode to renderer
        connect(m_controller, &SpatialController::gizmoModeChanged, this, [this]() {
            if (m_spatialView && m_spatialView->renderer())
                m_spatialView->renderer()->setGizmoMode(m_controller->gizmoMode());
        });

        // Grid snap: round position to nearest grid increment
        m_spatialView->setSnapCallback([this](double &x, double &y, double &z) {
            if (!m_controller->gridSnap())
                return;
            double g = m_controller->gridSize();
            x = std::round(x / g) * g;
            y = std::round(y / g) * g;
            z = std::round(z / g) * g;
        });

        // --- QML side panel ---
        m_sidePanel = new QQuickWidget(this);
        m_sidePanel->setResizeMode(QQuickWidget::SizeRootObjectToView);
        m_sidePanel->setClearColor(QColor(0x2a, 0x2a, 0x2a));
        m_sidePanel->setFixedWidth(280);

        // Expose controller to QML before loading source
        m_sidePanel->rootContext()->setContextProperty("spatialController", m_controller);

        // Load QML — try paths relative to the app binary.
        // Binary is at: build-v5/qmlui/qlcplus-qml.app/Contents/MacOS/qlcplus-qml
        // Source is at: qmlui/qml/spatial/LayoutPanel.qml (5 levels up from MacOS/)
        QString qmlPath;
        QStringList searchPaths = {
            // macOS .app bundle (5 levels up to source root)
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../../../../qmlui/qml/spatial/LayoutPanel.qml"),
            // Flat build (non-bundle, 2 levels up)
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../qmlui/qml/spatial/LayoutPanel.qml"),
        };

        for (const QString &p : searchPaths)
        {
            QFileInfo fi(p);
            if (fi.exists())
            {
                qmlPath = fi.absoluteFilePath();
                break;
            }
        }

        if (!qmlPath.isEmpty())
        {
            qDebug() << "[SpatialViewWindow] Loading QML:" << qmlPath;
            m_sidePanel->setSource(QUrl::fromLocalFile(qmlPath));
        }
        else
        {
            qWarning() << "[SpatialViewWindow] LayoutPanel.qml not found";
        }

        // --- Layout ---
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(viewportContainer, 1);  // stretch
        layout->addWidget(m_sidePanel, 0);         // fixed width
    }

    ~SpatialViewWindow() override
    {
        s_instance = nullptr;
    }

    static SpatialViewWindow *s_instance;

    static void createAndShow(Doc *doc)
    {
        if (s_instance)
        {
            s_instance->show();
            s_instance->raise();
            s_instance->activateWindow();
            return;
        }

        s_instance = new SpatialViewWindow(doc);

        QSettings settings;
        QVariant var = settings.value(SETTINGS_SPATIALVIEW_GEOMETRY);
        if (var.isValid())
        {
            s_instance->restoreGeometry(var.toByteArray());
        }
        else
        {
            QScreen *screen = QGuiApplication::primaryScreen();
            if (screen)
            {
                QRect screenGeo = screen->availableGeometry();
                s_instance->resize(screenGeo.width() * 3 / 4, screenGeo.height() * 3 / 4);
            }
            else
            {
                s_instance->resize(1024, 768);
            }
        }

        s_instance->show();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        QSettings settings;
        settings.setValue(SETTINGS_SPATIALVIEW_GEOMETRY, saveGeometry());
        m_spatialView->stopRendering();
        hide();
        event->ignore();
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        m_spatialView->startRendering();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        m_spatialView->stopRendering();
    }

private:
    Doc *m_doc;
    SpatialView *m_spatialView = nullptr;
    SpatialController *m_controller = nullptr;
    QQuickWidget *m_sidePanel = nullptr;
};

SpatialViewWindow *SpatialViewWindow::s_instance = nullptr;

// Include the MOC output for the class defined in this .cpp
#include "spatialviewwindow.moc"

// --- Public API (header-visible free function) ---

void showSpatialViewWindow(Doc *doc)
{
    SpatialViewWindow::createAndShow(doc);
}

QImage grabSpatialViewWindow()
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_spatialView)
        return QImage();

    // Let a couple frames render before capturing (avoids blank on first open)
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();

    // Request GPU framebuffer readback from bgfx
    inst->m_spatialView->requestViewportScreenshot();

    // Wait for bgfx::frame() to fire the callback (~1-2 frames at 60Hz)
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();

    // Get the viewport image from bgfx
    QImage viewportImg = inst->m_spatialView->takeViewportScreenshot();

    // Get the QML panel via QWidget::grab (works off-screen)
    QImage widgetImg = inst->grab().toImage();

    if (viewportImg.isNull())
    {
        // bgfx screenshot not ready — return widget grab (panel visible, viewport gray)
        return widgetImg;
    }

    // Composite: paint the bgfx viewport over the gray area in the widget grab.
    // widgetImg has devicePixelRatio=2, so QPainter coordinates are in logical pixels.
    // viewportImg is raw device pixels from bgfx. Draw it scaled down by DPR.
    qreal dpr = widgetImg.devicePixelRatio();
    int logicalW = qRound(viewportImg.width() / dpr);
    int logicalH = qRound(viewportImg.height() / dpr);

    QPainter painter(&widgetImg);
    painter.drawImage(QRect(0, 0, logicalW, logicalH), viewportImg);
    painter.end();

    return widgetImg;
}

void spatialViewSelectFixture(int32_t fixtureId)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_spatialView)
        inst->m_spatialView->selectFixture(fixtureId);
}

void spatialViewAddSelectedFixture(int32_t fixtureId)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_spatialView)
        return;

    if (fixtureId >= 0)
    {
        inst->m_spatialView->renderer()->addSelectedFixture(fixtureId);
        // Notify controller with updated count
        if (inst->m_controller)
        {
            int count = int(inst->m_spatialView->renderer()->selectedIds().size());
            inst->m_controller->notifySelectionChanged(
                inst->m_spatialView->renderer()->selectedFixture(), count);
        }
    }
}

QJsonArray spatialViewGetFixtureScreenPositions()
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_spatialView)
        return QJsonArray();

    auto *renderer = inst->m_spatialView->renderer();
    if (!renderer)
        return QJsonArray();

    auto *bgfxR = dynamic_cast<qlcrender::BgfxRenderer *>(renderer);
    if (!bgfxR)
        return QJsonArray();

    float dpr = float(inst->m_spatialView->devicePixelRatio());
    QJsonArray result;

    // Iterate over fixtures in the renderer
    for (const auto &f : bgfxR->fixtures())
    {
        // Skip ghosts (translucent)
        if (f.color[3] < 0.99f)
            continue;

        float worldPos[3] = { f.transform[12], f.transform[13], f.transform[14] };
        float screenX, screenY;
        bool visible;

        if (bgfxR->worldToScreen(worldPos, screenX, screenY, visible))
        {
            QJsonObject entry;
            entry["id"] = static_cast<int>(f.id);
            entry["screenX"] = qRound(screenX / dpr);  // device → logical
            entry["screenY"] = qRound(screenY / dpr);
            entry["visible"] = visible;
            entry["worldX"] = double(worldPos[0]);
            entry["worldY"] = double(worldPos[1]);
            entry["worldZ"] = double(worldPos[2]);
            result.append(entry);
        }
    }

    return result;
}

void spatialViewSetCamera(float yaw, float pitch, float distance)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_spatialView)
        inst->m_spatialView->setCameraOrbit(yaw, pitch, distance);
}

void spatialViewSetGizmoMode(int mode)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_controller)
        inst->m_controller->setGizmoMode(mode);
}

void spatialViewDrag(float x1, float y1, float x2, float y2, int steps)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_spatialView)
        return;

    QWindow *w = inst->m_spatialView;

    // Press at start
    QPoint startPos(qRound(x1), qRound(y1));
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, startPos);

    // Move in steps
    for (int i = 1; i <= steps; i++)
    {
        float t = float(i) / float(steps);
        int mx = qRound(x1 + (x2 - x1) * t);
        int my = qRound(y1 + (y2 - y1) * t);
        QTest::mouseMove(w, QPoint(mx, my));
    }

    // Release at end
    QPoint endPos(qRound(x2), qRound(y2));
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, endPos);
}
