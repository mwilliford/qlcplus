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
#include <QSplitter>
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
#include "calibratecontroller.h"
#include "spatialmodel.h"
#include "doc.h"
#include "simpledesk.h"
#include <cmath>
#include "bgfxrenderer.h"
#include <QPainter>
#include <QEventLoop>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

// ---------------------------------------------------------------------------
// SimpleDesk bridge for Focus mode DMX writes.
// App::App() calls spatialViewSetSimpleDesk(m_simpleDesk) once at startup.
// SpatialViewWindow's constructor reads this pointer when wiring up the
// SpatialController → SimpleDesk signals.
// ---------------------------------------------------------------------------

SimpleDesk *s_spatialViewSimpleDesk = nullptr;

void spatialViewSetSimpleDesk(SimpleDesk *desk)
{
    s_spatialViewSimpleDesk = desk;
}

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
    friend void spatialViewAlignSelection(const QString &);
    friend void spatialViewAddTruss(const QString &, double, double, double, double, double, double);
    friend void spatialViewSetMode(int);
    friend QString spatialViewCreateFocusPoint(double, double, double, const QString &);
    friend bool spatialViewDeleteFocusPoint(const QString &);
    friend bool spatialViewMoveFocusPoint(const QString &, double, double, double);
    friend bool spatialViewRenameFocusPoint(const QString &, const QString &);
    friend bool spatialViewAssignFixtureToFocusPoint(const QString &, int);
    friend bool spatialViewUnassignFixtureFromFocusPoint(const QString &, int);
    friend bool spatialViewAimAtFocusPoint(const QString &);
    friend void spatialViewSelectFocusPoint(const QString &);
    friend QJsonArray spatialViewGetFocusPoints();

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

        // Selected IDs callback for align
        m_controller->setSelectedIdsCallback([this]() -> std::vector<int32_t> {
            std::vector<int32_t> ids;
            if (m_spatialView && m_spatialView->renderer())
                for (int32_t id : m_spatialView->renderer()->selectedIds())
                    ids.push_back(id);
            return ids;
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

        // --- Focus mode wiring ---
        m_spatialView->setFocusModeCallback([this]() {
            return m_controller->mode() == SpatialController::Focus;
        });
        m_spatialView->setFocusAimCallback([this](double x, double y, double z) {
            m_controller->setFocusAim(x, y, z);
        });
        m_spatialView->setLiveDmxModeCallback([this]() {
            int m = m_controller->mode();
            return m == SpatialController::Calibrate || m == SpatialController::Focus;
        });
        m_spatialView->setSelectedFocusPointCallback([this]() {
            return m_controller->selectedFocusPointId();
        });

        // Selected focus point changed → re-send render data so the highlight updates.
        connect(m_controller, &SpatialController::selectedFocusPointChanged, this, [this]() {
            if (m_spatialView)
                m_spatialView->rebuildFocusPoints();  // need to expose this or use signal→slot
        });

        // Mode change: rebuild beam cones so Layout↔Calibrate switches between
        // home-position and live-DMX rendering immediately.
        connect(m_controller, &SpatialController::modeChanged, this, [this]() {
            if (m_spatialView)
                m_spatialView->rebuildBeamCones();
        });

        // Focus aim marker: update renderer when SpatialController aim changes.
        connect(m_controller, &SpatialController::focusAimChanged, this, [this]() {
            if (!m_spatialView || !m_spatialView->renderer())
                return;
            double x, y, z;
            m_controller->getFocusAim(&x, &y, &z);
            float pos[3] = { float(x), float(y), float(z) };
            m_spatialView->renderer()->setFocusAimMarker(
                m_controller->focusAimValid(), pos);
        });

        // Wire SimpleDesk DMX writes from Focus mode.
        // s_spatialViewSimpleDesk is set by spatialViewSetSimpleDesk() at startup.
        if (s_spatialViewSimpleDesk)
        {
            connect(m_controller, &SpatialController::focusDmxWrite,
                    s_spatialViewSimpleDesk, &SimpleDesk::setAbsoluteChannelValue);
            connect(m_controller, &SpatialController::focusDmxReset,
                    s_spatialViewSimpleDesk, &SimpleDesk::resetAbsoluteChannel);
        }

        // Snap: truss first, then grid
        m_spatialView->setSnapCallback([this](double &x, double &y, double &z) {
            // Try truss snap first (0.5m proximity)
            SpatialModel *sm = m_doc->spatialModel();
            double tx, ty, tz;
            if (sm->snapToTruss(x, y, z, tx, ty, tz, 0.5))
            {
                x = tx; y = ty; z = tz;
                return;
            }

            // Grid snap fallback
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
        m_sidePanel->setMinimumWidth(260);

        // Calibrate controller (owns observation/solver QML API)
        m_calibrateController = new CalibrateController(doc, this);

        // Expose controllers to QML before loading source
        m_sidePanel->rootContext()->setContextProperty("spatialController", m_controller);
        m_sidePanel->rootContext()->setContextProperty("calibrateController", m_calibrateController);

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

        // --- Layout: splitter so user can drag the side panel wider/narrower ---
        m_splitter = new QSplitter(Qt::Horizontal, this);
        m_splitter->addWidget(viewportContainer);
        m_splitter->addWidget(m_sidePanel);
        m_splitter->setCollapsible(0, false);
        m_splitter->setCollapsible(1, false);
        m_splitter->setHandleWidth(6);
        m_splitter->setStretchFactor(0, 1);  // viewport stretches, panel stays
        m_splitter->setStretchFactor(1, 0);
        m_splitter->setChildrenCollapsible(false);

        // Visible handle with hover effect (dark theme compatible)
        m_splitter->setStyleSheet(
            "QSplitter::handle { background: #444; }"
            "QSplitter::handle:hover { background: #4a9eff; }"
            "QSplitter::handle:pressed { background: #4a9eff; }"
        );

        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_splitter);

        // Initial panel width — will be overridden by saved geometry in showEvent
        m_splitter->setSizes(QList<int>() << 800 << 340);
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

        // Restore splitter state after show so widget has real size
        QVariant splitVar = settings.value("spatialview/splitter");
        if (splitVar.isValid())
        {
            s_instance->m_splitter->restoreState(splitVar.toByteArray());
        }
        else
        {
            // Force initial split sizes now that the widget has a real width
            int totalWidth = s_instance->width();
            int panelWidth = 340;
            s_instance->m_splitter->setSizes(QList<int>() << (totalWidth - panelWidth) << panelWidth);
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        QSettings settings;
        settings.setValue(SETTINGS_SPATIALVIEW_GEOMETRY, saveGeometry());
        if (m_splitter)
            settings.setValue("spatialview/splitter", m_splitter->saveState());
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
    CalibrateController *m_calibrateController = nullptr;
    QQuickWidget *m_sidePanel = nullptr;
    QSplitter *m_splitter = nullptr;
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

void spatialViewAlignSelection(const QString &axis)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_controller)
        inst->m_controller->alignSelection(axis);
}

void spatialViewAddTruss(const QString &name, double x1, double y1, double z1,
                          double x2, double y2, double z2)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_doc)
        return;

    static int counter = 0;
    SpatialModel::Truss truss;
    truss.id = QString("truss_%1").arg(counter++);
    truss.name = name.isEmpty() ? QString("Truss %1").arg(counter) : name;
    truss.start[0] = x1; truss.start[1] = y1; truss.start[2] = z1;
    truss.end[0] = x2; truss.end[1] = y2; truss.end[2] = z2;
    inst->m_doc->spatialModel()->addTruss(truss);
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

void spatialViewSetMode(int mode)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_controller)
        inst->m_controller->setMode(mode);
}

// --- Focus Point bridge implementations ---

QString spatialViewCreateFocusPoint(double x, double y, double z, const QString &name)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller)
        return QString();
    return inst->m_controller->createFocusPoint(x, y, z, name);
}

bool spatialViewDeleteFocusPoint(const QString &id)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller || !inst->m_doc) return false;
    // Only delete if it actually exists (to give the caller a useful bool).
    if (!inst->m_doc->spatialModel()->focusPoint(id)) return false;
    inst->m_controller->deleteFocusPoint(id);
    return true;
}

bool spatialViewMoveFocusPoint(const QString &id, double x, double y, double z)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller || !inst->m_doc) return false;
    if (!inst->m_doc->spatialModel()->focusPoint(id)) return false;
    inst->m_controller->moveFocusPoint(id, x, y, z);
    return true;
}

bool spatialViewRenameFocusPoint(const QString &id, const QString &name)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller || !inst->m_doc) return false;
    if (!inst->m_doc->spatialModel()->focusPoint(id)) return false;
    inst->m_controller->renameFocusPoint(id, name);
    return true;
}

bool spatialViewAssignFixtureToFocusPoint(const QString &fpId, int fixtureId)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller) return false;
    return inst->m_controller->assignFixtureToFocusPoint(fpId, fixtureId);
}

bool spatialViewUnassignFixtureFromFocusPoint(const QString &fpId, int fixtureId)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller) return false;
    return inst->m_controller->unassignFixtureFromFocusPoint(fpId, fixtureId);
}

bool spatialViewAimAtFocusPoint(const QString &id)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_controller || !inst->m_doc) return false;
    if (!inst->m_doc->spatialModel()->focusPoint(id)) return false;
    inst->m_controller->aimAtFocusPoint(id);
    return true;
}

void spatialViewSelectFocusPoint(const QString &id)
{
    auto *inst = SpatialViewWindow::s_instance;
    if (inst && inst->m_controller)
        inst->m_controller->setSelectedFocusPointId(id);
}

QJsonArray spatialViewGetFocusPoints()
{
    QJsonArray result;
    auto *inst = SpatialViewWindow::s_instance;
    if (!inst || !inst->m_doc || !inst->m_controller)
        return result;

    SpatialModel *sm = inst->m_doc->spatialModel();
    QString selId = inst->m_controller->selectedFocusPointId();

    for (const auto &fp : sm->focusPoints())
    {
        QJsonArray assigned;
        for (const QString &fid : fp.assignedFixtureIds)
            assigned.append(fid.toInt());

        QJsonObject obj{
            {"id",       fp.id},
            {"name",     fp.name},
            {"x",        fp.position[0]},
            {"y",        fp.position[1]},
            {"z",        fp.position[2]},
            {"assigned", assigned},
            {"selected", fp.id == selId},
        };
        result.append(obj);
    }
    return result;
}
