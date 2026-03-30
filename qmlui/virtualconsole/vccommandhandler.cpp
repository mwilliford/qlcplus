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
#include "vcpage.h"
#include "vcsoloframe.h"
#include "vclabel.h"
#include "vccuelist.h"
#include "qlcinputsource.h"
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

static VCFrame* findParentFrame(const QJsonObject &params, VirtualConsole *vc)
{
    int parentId = params.value("parentId").toInt(-1);
    if (parentId < 0)
    {
        // Default to the currently selected page
        VCPage *page = vc->page(vc->selectedPage());
        return page;
    }
    VCWidget *parent = vc->widget(static_cast<quint32>(parentId));
    if (parent)
    {
        VCFrame *frame = qobject_cast<VCFrame*>(parent);
        if (frame)
            return frame;
    }
    return nullptr;
}

static void applyCommonProperties(VCWidget *widget, const QJsonObject &params)
{
    if (params.contains("caption"))
        widget->setCaption(params["caption"].toString());

    QRectF geom = widget->geometry();
    bool hasGeometry = false;

    if (params.contains("x")) { geom.moveLeft(params["x"].toDouble()); hasGeometry = true; }
    if (params.contains("y")) { geom.moveTop(params["y"].toDouble()); hasGeometry = true; }
    if (params.contains("width")) { geom.setWidth(params["width"].toDouble()); hasGeometry = true; }
    if (params.contains("height")) { geom.setHeight(params["height"].toDouble()); hasGeometry = true; }

    if (hasGeometry)
        widget->setGeometry(geom);
}

/*****************************************************************************
 * create_widget
 *****************************************************************************/

static bool handleCreateWidget(const QJsonObject &params, QJsonObject &result,
                               VirtualConsole *vc, Doc *doc)
{
    Q_UNUSED(doc)

    QString type = params.value("widgetType").toString();
    if (type.isEmpty())
    {
        result["error"] = "Missing 'widgetType' parameter";
        return false;
    }

    VCFrame *parentFrame = findParentFrame(params, vc);
    if (!parentFrame)
    {
        result["error"] = "Invalid parentId or parent does not accept children";
        return false;
    }

    // Map protocol type names to v5 addWidget type strings
    QString v5Type = type;
    if (type == "SoloFrame") v5Type = "Solo frame";
    else if (type == "CueList") v5Type = "Cue list";
    else if (type == "SpeedDial") v5Type = "Speed";
    else if (type == "AudioTriggers") v5Type = "Audio Triggers";

    int x = params.value("x").toInt(10);
    int y = params.value("y").toInt(10);

    // VCFrame::addWidget handles: new, ID, Tardis, geometry, setupWidget, render
    VCWidget *widget = parentFrame->addWidget(parentFrame->renderItem(), v5Type, QPoint(x, y));
    if (!widget)
    {
        result["error"] = QString("Unsupported widget type: %1").arg(type);
        return false;
    }

    // Apply type-specific properties after creation
    VCButton *btn = qobject_cast<VCButton*>(widget);
    if (btn)
    {
        if (params.contains("functionId"))
            btn->setFunctionID(static_cast<quint32>(params["functionId"].toInt()));
        if (params.contains("action"))
            btn->setActionType(VCButton::stringToAction(params["action"].toString()));
    }

    VCSlider *slider = qobject_cast<VCSlider*>(widget);
    if (slider)
    {
        if (params.contains("sliderMode"))
            slider->setSliderMode(VCSlider::stringToSliderMode(params["sliderMode"].toString()));
        if (params.contains("controlledFunctionId"))
            slider->setControlledFunction(static_cast<quint32>(params["controlledFunctionId"].toInt()));
        // Accept "playbackFunctionId" as alias for backward compatibility
        if (params.contains("playbackFunctionId"))
            slider->setControlledFunction(static_cast<quint32>(params["playbackFunctionId"].toInt()));
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
            slider->setRangeLowLimit(params["levelLowLimit"].toDouble());
        if (params.contains("levelHighLimit"))
            slider->setRangeHighLimit(params["levelHighLimit"].toDouble());
    }

    VCFrame *frame = qobject_cast<VCFrame*>(widget);
    if (frame)
    {
        if (params.contains("showHeader"))
            frame->setShowHeader(params["showHeader"].toBool());
    }

    VCSoloFrame *solo = qobject_cast<VCSoloFrame*>(widget);
    if (solo)
    {
        if (params.contains("mixing"))
            solo->setSoloframeMixing(params["mixing"].toBool());
    }

    VCCueList *cueList = qobject_cast<VCCueList*>(widget);
    if (cueList)
    {
        if (params.contains("chaserFunctionId"))
            cueList->setChaserID(static_cast<quint32>(params["chaserFunctionId"].toInt()));
    }

    // Apply common properties (caption, geometry overrides) after type-specific setup
    applyCommonProperties(widget, params);

    doc->setModified();

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

    applyCommonProperties(widget, params);

    VCButton *btn = qobject_cast<VCButton*>(widget);
    if (btn)
    {
        if (params.contains("functionId"))
            btn->setFunctionID(static_cast<quint32>(params["functionId"].toInt()));
        if (params.contains("action"))
            btn->setActionType(VCButton::stringToAction(params["action"].toString()));
    }

    VCSlider *slider = qobject_cast<VCSlider*>(widget);
    if (slider)
    {
        if (params.contains("sliderMode"))
            slider->setSliderMode(VCSlider::stringToSliderMode(params["sliderMode"].toString()));
        if (params.contains("controlledFunctionId"))
            slider->setControlledFunction(static_cast<quint32>(params["controlledFunctionId"].toInt()));
        if (params.contains("playbackFunctionId"))
            slider->setControlledFunction(static_cast<quint32>(params["playbackFunctionId"].toInt()));
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
            slider->setRangeLowLimit(params["levelLowLimit"].toDouble());
        if (params.contains("levelHighLimit"))
            slider->setRangeHighLimit(params["levelHighLimit"].toDouble());
    }

    VCFrame *frame = qobject_cast<VCFrame*>(widget);
    if (frame)
    {
        if (params.contains("showHeader"))
            frame->setShowHeader(params["showHeader"].toBool());
    }

    VCCueList *cueList = qobject_cast<VCCueList*>(widget);
    if (cueList)
    {
        if (params.contains("chaserFunctionId"))
            cueList->setChaserID(static_cast<quint32>(params["chaserFunctionId"].toInt()));
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
    Q_UNUSED(doc)

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

    // Don't allow deleting pages (root frames)
    if (qobject_cast<VCPage*>(widget) != nullptr)
    {
        result["error"] = "Cannot delete a page";
        return false;
    }

    // Use v5's deleteVCWidgets which handles page map, children, Tardis, and deletion
    QVariantList idList;
    idList.append(widgetId);
    vc->deleteVCWidgets(idList);

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
    Q_UNUSED(doc)

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

    quint32 universe = static_cast<quint32>(params["universe"].toInt());
    quint32 channel = static_cast<quint32>(params["channel"].toInt());

    // Use v5's input source API
    vc->createAndAddInputSource(widget, universe, channel);

    // Update feedback values if provided
    if (params.contains("lowerValue") || params.contains("upperValue"))
    {
        quint8 lower = static_cast<quint8>(params.value("lowerValue").toInt(0));
        quint8 upper = static_cast<quint8>(params.value("upperValue").toInt(255));
        quint8 monitor = static_cast<quint8>(params.value("monitorValue").toInt(0));
        widget->updateInputSourceFeedbackValues(universe, channel, lower, upper, monitor);
    }

    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_changed";

    qDebug() << "[VCCommandHandler] Set input source on widget id=" << widgetId
             << "uni=" << universe << "ch=" << channel;
    return true;
}

/*****************************************************************************
 * clear_widget_input
 *****************************************************************************/

static bool handleClearWidgetInput(const QJsonObject &params, QJsonObject &result,
                                   VirtualConsole *vc, Doc *doc)
{
    Q_UNUSED(doc)

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

    quint32 universe = static_cast<quint32>(params["universe"].toInt());
    quint32 channel = static_cast<quint32>(params["channel"].toInt());
    quint32 inputId = static_cast<quint32>(params.value("inputId").toInt(0));

    vc->deleteInputSource(widget, inputId, universe, channel);
    doc->setModified();

    result["widgetId"] = static_cast<int>(widgetId);
    result["widget"] = serializeWidget(widget);
    result["deltaAction"] = "vc_widget_changed";

    qDebug() << "[VCCommandHandler] Cleared input source on widget id=" << widgetId;
    return true;
}
