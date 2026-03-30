/*
  Q Light Controller Plus
  vcserializer.cpp

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

#include "vcserializer.h"
#include "virtualconsole.h"
#include "vcwidget.h"
#include "vcframe.h"
#include "vcpage.h"
#include "vcsoloframe.h"
#include "vcbutton.h"
#include "vcslider.h"
#include "vccuelist.h"
#include "vcspeeddial.h"
#include "vcxypad.h"
#include "vcxypadpreset.h"
#include "vcanimation.h"
#include "vcclock.h"
#include "vclabel.h"
#include "vcaudiotriggers.h"
#include "qlcinputsource.h"
#include "scenevalue.h"

QJsonArray serializeInputSources(VCWidget *widget)
{
    QJsonArray sources;
    QList<QSharedPointer<QLCInputSource>> srcList = widget->inputSources();
    for (const QSharedPointer<QLCInputSource> &src : srcList)
    {
        if (src.isNull() || !src->isValid())
            continue;

        QJsonObject srcObj;
        srcObj["id"] = (int)src->id();
        srcObj["universe"] = (int)src->universe();
        srcObj["channel"] = (int)src->channel();
        srcObj["lowerValue"] = (int)src->feedbackValue(QLCInputFeedback::LowerValue);
        srcObj["upperValue"] = (int)src->feedbackValue(QLCInputFeedback::UpperValue);
        srcObj["monitorValue"] = (int)src->feedbackValue(QLCInputFeedback::MonitorValue);
        sources.append(srcObj);
    }
    return sources;
}

static void serializeWidgetBase(VCWidget *widget, QJsonObject &obj)
{
    obj["id"] = (int)widget->id();
    obj["type"] = VCWidget::typeToString(widget->type());
    obj["caption"] = widget->caption();
    obj["page"] = widget->page();

    QRectF geom = widget->geometry();
    obj["x"] = (int)geom.x();
    obj["y"] = (int)geom.y();
    obj["width"] = (int)geom.width();
    obj["height"] = (int)geom.height();

    QJsonArray inputs = serializeInputSources(widget);
    if (!inputs.isEmpty())
        obj["inputSources"] = inputs;
}

static void serializeButton(VCButton *button, QJsonObject &obj)
{
    obj["functionId"] = (int)button->functionID();
    obj["action"] = VCButton::actionToString(button->actionType());
}

static void serializeSlider(VCSlider *slider, QJsonObject &obj)
{
    obj["sliderMode"] = VCSlider::sliderModeToString(slider->sliderMode());
    obj["widgetStyle"] = slider->widgetStyleToString(slider->widgetStyle());

    if (slider->sliderMode() == VCSlider::Level)
    {
        obj["levelLowLimit"] = (int)slider->rangeLowLimit();
        obj["levelHighLimit"] = (int)slider->rangeHighLimit();

        QJsonArray channels;
        for (const SceneValue &sv : slider->levelChannels())
        {
            QJsonObject ch;
            ch["fixtureId"] = (int)sv.fxi;
            ch["channel"] = (int)sv.channel;
            channels.append(ch);
        }
        if (!channels.isEmpty())
            obj["levelChannels"] = channels;
    }
    else if (slider->sliderMode() == VCSlider::Adjust)
    {
        obj["controlledFunctionId"] = (int)slider->controlledFunction();
        obj["controlledAttribute"] = slider->controlledAttribute();
    }
}

static void serializeFrame(VCFrame *frame, QJsonObject &obj)
{
    obj["showHeader"] = frame->showHeader();
    obj["collapsed"] = frame->isCollapsed();

    int totalPages = frame->totalPagesNumber();
    obj["multipage"] = (totalPages > 1);
    obj["totalPages"] = totalPages;
    obj["currentPage"] = frame->currentPage();

    obj["children"] = serializeChildren(frame);
}

static void serializeSoloFrame(VCSoloFrame *soloFrame, QJsonObject &obj)
{
    serializeFrame(soloFrame, obj);
    obj["mixing"] = soloFrame->soloframeMixing();
}

static void serializeCueList(VCCueList *cueList, QJsonObject &obj)
{
    obj["chaserFunctionId"] = (int)cueList->chaserID();
}

static void serializeSpeedDial(VCSpeedDial *speedDial, QJsonObject &obj)
{
    QJsonArray funcIds;
    QMap<quint32, VCSpeedDial::VCSpeedDialFunction> funcs = speedDial->functions();
    for (auto it = funcs.constBegin(); it != funcs.constEnd(); ++it)
    {
        funcIds.append((int)it.key());
    }
    if (!funcIds.isEmpty())
        obj["functions"] = funcIds;
}

static void serializeXYPad(VCXYPad *xyPad, QJsonObject &obj)
{
    QJsonArray fixtures;
    for (const VCXYPad::XYPadFixture &fxi : xyPad->fixtures())
    {
        QJsonObject fxiObj;
        fxiObj["fixtureId"] = (int)fxi.m_head.fxi;
        fxiObj["headIndex"] = fxi.m_head.head;
        fixtures.append(fxiObj);
    }
    if (!fixtures.isEmpty())
        obj["xypadFixtures"] = fixtures;

    QJsonArray presets;
    for (VCXYPadPreset *preset : xyPad->presetList())
    {
        QJsonObject pObj;
        pObj["id"] = (int)preset->m_id;
        pObj["type"] = VCXYPadPreset::typeToString(preset->m_type);
        pObj["name"] = preset->m_name;
        if (preset->m_type == VCXYPadPreset::EFX || preset->m_type == VCXYPadPreset::Scene)
            pObj["functionId"] = (int)preset->m_funcID;
        if (preset->m_type == VCXYPadPreset::Position)
        {
            pObj["posX"] = preset->m_dmxPos.x();
            pObj["posY"] = preset->m_dmxPos.y();
        }
        presets.append(pObj);
    }
    if (!presets.isEmpty())
        obj["xypadPresets"] = presets;
}

static void serializeAnimation(VCAnimation *animation, QJsonObject &obj)
{
    obj["functionId"] = (int)animation->functionID();
    obj["instantApply"] = animation->instantChanges();
}

static void serializeClock(VCClock *clock, QJsonObject &obj)
{
    obj["clockType"] = clock->typeToString(clock->clockType());
}

QJsonObject serializeWidget(VCWidget *widget)
{
    QJsonObject obj;
    serializeWidgetBase(widget, obj);

    // SoloFrame check must come before Frame (SoloFrame inherits VCFrame)
    VCSoloFrame *soloFrame = qobject_cast<VCSoloFrame*>(widget);
    if (soloFrame != nullptr)
    {
        serializeSoloFrame(soloFrame, obj);
        return obj;
    }

    VCFrame *frame = qobject_cast<VCFrame*>(widget);
    if (frame != nullptr)
    {
        serializeFrame(frame, obj);
        return obj;
    }

    VCButton *button = qobject_cast<VCButton*>(widget);
    if (button != nullptr)
    {
        serializeButton(button, obj);
        return obj;
    }

    VCSlider *slider = qobject_cast<VCSlider*>(widget);
    if (slider != nullptr)
    {
        serializeSlider(slider, obj);
        return obj;
    }

    VCCueList *cueList = qobject_cast<VCCueList*>(widget);
    if (cueList != nullptr)
    {
        serializeCueList(cueList, obj);
        return obj;
    }

    VCSpeedDial *speedDial = qobject_cast<VCSpeedDial*>(widget);
    if (speedDial != nullptr)
    {
        serializeSpeedDial(speedDial, obj);
        return obj;
    }

    VCXYPad *xyPad = qobject_cast<VCXYPad*>(widget);
    if (xyPad != nullptr)
    {
        serializeXYPad(xyPad, obj);
        return obj;
    }

    VCAnimation *animation = qobject_cast<VCAnimation*>(widget);
    if (animation != nullptr)
    {
        serializeAnimation(animation, obj);
        return obj;
    }

    VCClock *clock = qobject_cast<VCClock*>(widget);
    if (clock != nullptr)
    {
        serializeClock(clock, obj);
        return obj;
    }

    // VCLabel, VCAudioTriggers, and unknown types — base fields only
    return obj;
}

QJsonArray serializeChildren(VCFrame *frame)
{
    QJsonArray children;
    QList<VCWidget*> childWidgets = frame->children(false);
    for (VCWidget *child : childWidgets)
    {
        children.append(serializeWidget(child));
    }
    return children;
}

QJsonObject serializeVirtualConsole(VirtualConsole *vc)
{
    QJsonObject vcObj;

    // Serialize all pages (v5 has multiple VCPage objects)
    QJsonArray allWidgets;
    for (int i = 0; i < vc->pagesCount(); i++)
    {
        VCPage *pg = vc->page(i);
        if (pg == nullptr)
            continue;

        QList<VCWidget*> pageChildren = pg->children(false);
        for (VCWidget *child : pageChildren)
        {
            allWidgets.append(serializeWidget(child));
        }
    }

    vcObj["widgets"] = allWidgets;
    return vcObj;
}
