/*
  Q Light Controller Plus
  vccommandhandler.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QJsonArray>
#include <QDebug>

#include "vccommandhandler.h"
#include "vcserializer.h"
#include "virtualconsole.h"
#include "vcbutton.h"
#include "vcslider.h"
#include "vcframe.h"
#include "vcsoloframe.h"
#include "vclabel.h"
#include "vccuelist.h"
#include "qlcinputsource.h"
#include "qlcinputfeedback.h"
#include "doc.h"

static bool handleCreateWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc);
static bool handleModifyWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc);
static bool handleDeleteWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc);
static bool handleSetWidgetInput(const QJsonObject &params, QJsonObject &result,
                                 VirtualConsole *vc, Doc *doc);
static bool handleClearWidgetInput(const QJsonObject &params, QJsonObject &result,
                                   VirtualConsole *vc, Doc *doc);

bool handleVCCommand(const QString &command, const QJsonObject &params,
                     QJsonObject &result, VirtualConsole *vc, Doc *doc)
{
    if (command == "create_widget")
        return handleCreateWidget(params, result, vc, doc);
    else if (command == "modify_widget")
        return handleModifyWidget(params, result, vc, doc);
    else if (command == "delete_widget")
        return handleDeleteWidget(params, result, vc, doc);
    else if (command == "set_widget_input")
        return handleSetWidgetInput(params, result, vc, doc);
    else if (command == "clear_widget_input")
        return handleClearWidgetInput(params, result, vc, doc);

    result["error"] = QString("Unknown VC command: %1").arg(command);
    return false;
}

/*****************************************************************************
 * Helpers
 *****************************************************************************/

static VCWidget* findParent(const QJsonObject &params, VirtualConsole *vc)
{
    int parentId = params.value("parentId").toInt(-1);
    if (parentId < 0)
        return vc->contents();  // root frame
    VCWidget *parent = vc->widget(static_cast<quint32>(parentId));
    if (parent && parent->allowChildren())
        return parent;
    return nullptr;
}

static void applyCommonProperties(VCWidget *widget, const QJsonObject &params)
{
    if (params.contains("caption"))
        widget->setCaption(params["caption"].toString());

    // Geometry: explicit x/y/width/height override default position
    bool hasGeometry = false;
    int x = widget->x(), y = widget->y();
    int w = widget->width(), h = widget->height();

    if (params.contains("x")) { x = params["x"].toInt(); hasGeometry = true; }
    if (params.contains("y")) { y = params["y"].toInt(); hasGeometry = true; }
    if (params.contains("width")) { w = params["width"].toInt(); hasGeometry = true; }
    if (params.contains("height")) { h = params["height"].toInt(); hasGeometry = true; }

    if (hasGeometry)
    {
        widget->move(QPoint(x, y));
        widget->resize(QSize(w, h));
    }
}

/*****************************************************************************
 * create_widget
 *****************************************************************************/

static bool handleCreateWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc)
{
    // Widget type is in "widgetType" (not "type", which is the protocol command name)
    QString type = params.value("widgetType").toString();
    if (type.isEmpty())
    {
        result["error"] = "Missing 'widgetType' parameter";
        return false;
    }

    VCWidget *parent = findParent(params, vc);
    if (!parent)
    {
        result["error"] = "Invalid parentId or parent does not accept children";
        return false;
    }

    VCWidget *widget = nullptr;

    if (type == "Button")
    {
        VCButton *btn = new VCButton(parent, doc);
        if (params.contains("functionId"))
            btn->setFunction(static_cast<quint32>(params["functionId"].toInt()));
        if (params.contains("action"))
        {
            QString actionStr = params["action"].toString();
            btn->setAction(VCButton::stringToAction(actionStr));
        }
        widget = btn;
    }
    else if (type == "Slider")
    {
        VCSlider *slider = new VCSlider(parent, doc);
        if (params.contains("sliderMode"))
        {
            QString modeStr = params["sliderMode"].toString();
            slider->setSliderMode(VCSlider::stringToSliderMode(modeStr));
        }
        if (params.contains("playbackFunctionId"))
            slider->setPlaybackFunction(static_cast<quint32>(params["playbackFunctionId"].toInt()));
        if (params.contains("levelChannels"))
        {
            QJsonArray channels = params["levelChannels"].toArray();
            for (const QJsonValue &v : channels)
            {
                QJsonObject ch = v.toObject();
                slider->addLevelChannel(
                    static_cast<quint32>(ch["fixtureId"].toInt()),
                    static_cast<quint32>(ch["channel"].toInt()));
            }
        }
        if (params.contains("levelLowLimit"))
            slider->setLevelLowLimit(static_cast<uchar>(params["levelLowLimit"].toInt()));
        if (params.contains("levelHighLimit"))
            slider->setLevelHighLimit(static_cast<uchar>(params["levelHighLimit"].toInt()));
        widget = slider;
    }
    else if (type == "Frame")
    {
        VCFrame *frame = new VCFrame(parent, doc, true);
        if (params.contains("showHeader"))
            frame->setHeaderVisible(params["showHeader"].toBool());
        widget = frame;
    }
    else if (type == "SoloFrame")
    {
        VCSoloFrame *solo = new VCSoloFrame(parent, doc, true);
        if (params.contains("showHeader"))
            solo->setHeaderVisible(params["showHeader"].toBool());
        if (params.contains("mixing"))
            solo->setSoloframeMixing(params["mixing"].toBool());
        widget = solo;
    }
    else if (type == "Label")
    {
        widget = new VCLabel(parent, doc);
    }
    else if (type == "CueList")
    {
        VCCueList *cueList = new VCCueList(parent, doc);
        if (params.contains("chaserFunctionId"))
            cueList->setChaser(static_cast<quint32>(params["chaserFunctionId"].toInt()));
        widget = cueList;
    }
    else
    {
        result["error"] = QString("Unsupported widget type: %1").arg(type);
        return false;
    }

    // Register in VC and connect to parent frame
    vc->setupWidget(widget, parent);

    // Apply common properties (caption, geometry) after setupWidget
    // so they override the default position from lastClickPoint
    applyCommonProperties(widget, params);

    doc->setModified();

    // Build result
    result["widgetId"] = static_cast<int>(widget->id());
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_added";

    qDebug() << "[VCCommandHandler] Created" << type << "widget id=" << widget->id();
    return true;
}

/*****************************************************************************
 * modify_widget
 *****************************************************************************/

static bool handleModifyWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc)
{
    if (!params.contains("widgetId"))
    {
        result["error"] = "Missing 'widgetId' parameter";
        return false;
    }

    quint32 widgetId = static_cast<quint32>(params["widgetId"].toInt());
    VCWidget *widget = vc->widget(widgetId);
    if (!widget)
    {
        result["error"] = QString("Widget %1 not found").arg(widgetId);
        return false;
    }

    // Common properties
    applyCommonProperties(widget, params);

    // Button-specific
    VCButton *btn = qobject_cast<VCButton*>(widget);
    if (btn)
    {
        if (params.contains("functionId"))
            btn->setFunction(static_cast<quint32>(params["functionId"].toInt()));
        if (params.contains("action"))
            btn->setAction(VCButton::stringToAction(params["action"].toString()));
    }

    // Slider-specific
    VCSlider *slider = qobject_cast<VCSlider*>(widget);
    if (slider)
    {
        if (params.contains("sliderMode"))
            slider->setSliderMode(VCSlider::stringToSliderMode(params["sliderMode"].toString()));
        if (params.contains("playbackFunctionId"))
            slider->setPlaybackFunction(static_cast<quint32>(params["playbackFunctionId"].toInt()));
        if (params.contains("levelChannels"))
        {
            slider->clearLevelChannels();
            QJsonArray channels = params["levelChannels"].toArray();
            for (const QJsonValue &v : channels)
            {
                QJsonObject ch = v.toObject();
                slider->addLevelChannel(
                    static_cast<quint32>(ch["fixtureId"].toInt()),
                    static_cast<quint32>(ch["channel"].toInt()));
            }
        }
        if (params.contains("levelLowLimit"))
            slider->setLevelLowLimit(static_cast<uchar>(params["levelLowLimit"].toInt()));
        if (params.contains("levelHighLimit"))
            slider->setLevelHighLimit(static_cast<uchar>(params["levelHighLimit"].toInt()));
    }

    // Frame-specific
    VCFrame *frame = qobject_cast<VCFrame*>(widget);
    if (frame)
    {
        if (params.contains("showHeader"))
            frame->setHeaderVisible(params["showHeader"].toBool());
    }

    // CueList-specific
    VCCueList *cueList = qobject_cast<VCCueList*>(widget);
    if (cueList)
    {
        if (params.contains("chaserFunctionId"))
            cueList->setChaser(static_cast<quint32>(params["chaserFunctionId"].toInt()));
    }

    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_changed";

    qDebug() << "[VCCommandHandler] Modified widget id=" << widgetId;
    return true;
}

/*****************************************************************************
 * delete_widget
 *****************************************************************************/

static bool handleDeleteWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc)
{
    if (!params.contains("widgetId"))
    {
        result["error"] = "Missing 'widgetId' parameter";
        return false;
    }

    quint32 widgetId = static_cast<quint32>(params["widgetId"].toInt());
    VCWidget *widget = vc->widget(widgetId);
    if (!widget)
    {
        result["error"] = QString("Widget %1 not found").arg(widgetId);
        return false;
    }

    // Don't allow deleting the root frame
    if (widget == vc->contents())
    {
        result["error"] = "Cannot delete the root frame";
        return false;
    }

    // Remove from widgets map (including children)
    vc->removeWidgetFromMap(widget);

    // Disconnect from parent frame's page map
    VCWidget *parent = qobject_cast<VCWidget*>(widget->parentWidget());
    if (parent)
        vc->disconnectWidgetFromParent(widget, parent);

    widget->deleteLater();
    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["deltaAction"] = "vc_widget_removed";

    qDebug() << "[VCCommandHandler] Deleted widget id=" << widgetId;
    return true;
}

/*****************************************************************************
 * set_widget_input
 *****************************************************************************/

static bool handleSetWidgetInput(const QJsonObject &params, QJsonObject &result,
                                 VirtualConsole *vc, Doc *doc)
{
    if (!params.contains("widgetId"))
    {
        result["error"] = "Missing 'widgetId' parameter";
        return false;
    }

    quint32 widgetId = static_cast<quint32>(params["widgetId"].toInt());
    VCWidget *widget = vc->widget(widgetId);
    if (!widget)
    {
        result["error"] = QString("Widget %1 not found").arg(widgetId);
        return false;
    }

    quint8 inputId = static_cast<quint8>(params.value("inputId").toInt(0));
    quint32 universe = static_cast<quint32>(params["universe"].toInt());
    quint32 channel = static_cast<quint32>(params["channel"].toInt());

    QSharedPointer<QLCInputSource> source(new QLCInputSource(universe, channel));

    if (params.contains("lowerValue"))
        source->setFeedbackValue(QLCInputFeedback::LowerValue,
                                 static_cast<uchar>(params["lowerValue"].toInt()));
    if (params.contains("upperValue"))
        source->setFeedbackValue(QLCInputFeedback::UpperValue,
                                 static_cast<uchar>(params["upperValue"].toInt()));

    widget->setInputSource(source, inputId);
    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_changed";

    qDebug() << "[VCCommandHandler] Set input source on widget id=" << widgetId
             << "inputId=" << inputId << "uni=" << universe << "ch=" << channel;
    return true;
}

/*****************************************************************************
 * clear_widget_input
 *****************************************************************************/

static bool handleClearWidgetInput(const QJsonObject &params, QJsonObject &result,
                                   VirtualConsole *vc, Doc *doc)
{
    if (!params.contains("widgetId"))
    {
        result["error"] = "Missing 'widgetId' parameter";
        return false;
    }

    quint32 widgetId = static_cast<quint32>(params["widgetId"].toInt());
    VCWidget *widget = vc->widget(widgetId);
    if (!widget)
    {
        result["error"] = QString("Widget %1 not found").arg(widgetId);
        return false;
    }

    quint8 inputId = static_cast<quint8>(params.value("inputId").toInt(0));

    // Set an invalid input source to clear the binding
    QSharedPointer<QLCInputSource> empty(new QLCInputSource());
    widget->setInputSource(empty, inputId);
    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_changed";

    qDebug() << "[VCCommandHandler] Cleared input source on widget id=" << widgetId
             << "inputId=" << inputId;
    return true;
}
