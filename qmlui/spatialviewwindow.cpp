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
#include "bgfxrenderer.h"
#include <QPainter>
#include <QEventLoop>
#include <QTimer>

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

        // --- QML side panel ---
        m_sidePanel = new QQuickWidget(this);
        m_sidePanel->setResizeMode(QQuickWidget::SizeRootObjectToView);
        m_sidePanel->setClearColor(QColor(0x2a, 0x2a, 0x2a));
        m_sidePanel->setFixedWidth(280);

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

    // Request GPU framebuffer readback from bgfx
    inst->m_spatialView->requestViewportScreenshot();

    // Spin the event loop for ~50ms to let bgfx::frame() fire the callback.
    // The screenshot is delivered during bgfx::frame() which runs on a 60Hz timer.
    QEventLoop loop;
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
