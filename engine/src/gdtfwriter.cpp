/*
  Q Light Controller Plus
  gdtfwriter.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "gdtfwriter.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QSet>
#include <QString>
#include <QVector>

#include "Include/VectorworksMVR.h"

#include "qlcchannel.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"

using namespace VectorworksMVR;
using namespace VectorworksMVR::GdtfDefines;

namespace {

// Deterministic UUID derived from fixture identity so round-trips are stable.
MvrUUID deterministicUuid(const QString &salt)
{
    const QByteArray md5 = QCryptographicHash::hash(
        salt.toUtf8(), QCryptographicHash::Md5);
    auto packU32 = [&](int offset) -> Uint32 {
        return static_cast<Uint32>(
            (static_cast<uchar>(md5[offset    ]) << 24) |
            (static_cast<uchar>(md5[offset + 1]) << 16) |
            (static_cast<uchar>(md5[offset + 2]) <<  8) |
             static_cast<uchar>(md5[offset + 3]));
    };
    return MvrUUID(packU32(0), packU32(4), packU32(8), packU32(12));
}

STransformMatrix identityMatrix()
{
    STransformMatrix m;
    m.ux = 1; m.vx = 0; m.wx = 0; m.ox = 0;
    m.uy = 0; m.vy = 1; m.wy = 0; m.oy = 0;
    m.uz = 0; m.vz = 0; m.wz = 1; m.oz = 0;
    return m;
}

bool isDimmerPreset(QLCChannel::Preset p)
{
    return p == QLCChannel::IntensityDimmer
        || p == QLCChannel::IntensityDimmerFine
        || p == QLCChannel::IntensityMasterDimmer
        || p == QLCChannel::IntensityMasterDimmerFine;
}

} // namespace

QByteArray GDTFWriter::writeSynthetic(const QLCFixtureDef *def,
                                      const QLCFixtureMode *mode,
                                      QString *outError)
{
    auto fail = [&](const QString &msg) -> QByteArray {
        if (outError) *outError = msg;
        qWarning() << "[GDTFWriter]" << msg;
        return QByteArray();
    };

    if (def == nullptr || mode == nullptr)
        return fail(QStringLiteral("null QLCFixtureDef or QLCFixtureMode"));

    IGdtfFixturePtr gdtf(IID_IGdtfFixture);

    const QString modeName = mode->name().isEmpty()
        ? QStringLiteral("Mode 1") : mode->name();
    const QString mfgStr = def->manufacturer().isEmpty()
        ? QStringLiteral("Unknown") : def->manufacturer();
    const QString modelStr = def->model().isEmpty()
        ? QStringLiteral("Unknown") : def->model();

    const QString salt = mfgStr + QLatin1Char('|')
                       + modelStr + QLatin1Char('|') + modeName;
    const MvrUUID uuid = deterministicUuid(salt);

    const QByteArray mfgUtf8 = mfgStr.toUtf8();
    const QByteArray modelUtf8 = modelStr.toUtf8();

    VCOMError err = gdtf->OpenForWrite(
        modelUtf8.constData(),
        mfgUtf8.constData(),
        uuid);
    if (err != kVCOMError_NoError)
        return fail(QStringLiteral("OpenForWrite failed (err=%1)").arg(err));

    gdtf->SetFixtureTypeDescription("Synthesized from QXF by QLC+ (MVR-3a)");
    gdtf->SetShortName(modelUtf8.left(16).constData());
    gdtf->SetLongName(modelUtf8.constData());
    gdtf->SetCanHaveChildren(false);

    // --- Feature groups + features (required by GDTF for attribute binding) ---
    IGdtfFeatureGroupPtr fgPosition, fgDimmer, fgControl;
    IGdtfFeaturePtr featPosition, featDimmer, featControl;

    gdtf->CreateFeatureGroup("Position", "Position", &fgPosition);
    fgPosition->CreateFeature("PanTilt", &featPosition);

    gdtf->CreateFeatureGroup("Dimmer", "Dimmer", &fgDimmer);
    fgDimmer->CreateFeature("Dimmer", &featDimmer);

    gdtf->CreateFeatureGroup("Control", "Control", &fgControl);
    fgControl->CreateFeature("Control", &featControl);

    // --- Attributes ---
    IGdtfAttributePtr attrPan, attrTilt, attrDimmer, attrNoFeature;

    gdtf->CreateAttribute("Pan", "Pan", &attrPan);
    attrPan->SetFeature(featPosition);
    attrPan->SetPhysicalUnit(EGdtfPhysicalUnit::Angle);

    gdtf->CreateAttribute("Tilt", "Tilt", &attrTilt);
    attrTilt->SetFeature(featPosition);
    attrTilt->SetPhysicalUnit(EGdtfPhysicalUnit::Angle);

    gdtf->CreateAttribute("Dimmer", "Dimmer", &attrDimmer);
    attrDimmer->SetFeature(featDimmer);
    attrDimmer->SetPhysicalUnit(EGdtfPhysicalUnit::None);

    gdtf->CreateAttribute("NoFeature", "NoFeature", &attrNoFeature);
    attrNoFeature->SetFeature(featControl);
    attrNoFeature->SetPhysicalUnit(EGdtfPhysicalUnit::None);

    // --- Decide geometry tree shape from mode + physical ---
    const bool hasPan =
        mode->channelNumber(QLCChannel::Pan, QLCChannel::MSB)
        != QLCChannel::invalid();
    const bool hasTilt =
        mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB)
        != QLCChannel::invalid();

    const QLCPhysical phy = mode->physical();
    const bool isMirror = phy.focusType().compare(
        QStringLiteral("Mirror"), Qt::CaseInsensitive) == 0;
    const double panRange  = phy.focusPanMax()  > 0 ? phy.focusPanMax()  : 540.0;
    const double tiltRange = phy.focusTiltMax() > 0 ? phy.focusTiltMax() : 270.0;
    const double beamAngle  = phy.lensDegreesMin() > 0 ? phy.lensDegreesMin() : 10.0;
    const double fieldAngle = phy.lensDegreesMax() > 0 ? phy.lensDegreesMax() : beamAngle;

    // --- Models (primitive-only, no mesh files) ---
    IGdtfModelPtr modelBase, modelYoke, modelHead, modelLamp;

    gdtf->CreateModel("Base", &modelBase);
    modelBase->SetPrimitiveType(eGdtfModel_PrimitiveType_Base);

    if (hasPan)
    {
        gdtf->CreateModel("Yoke", &modelYoke);
        modelYoke->SetPrimitiveType(eGdtfModel_PrimitiveType_Yoke);
    }
    if (hasTilt)
    {
        gdtf->CreateModel("Head", &modelHead);
        modelHead->SetPrimitiveType(isMirror
            ? eGdtfModel_PrimitiveType_Scanner
            : eGdtfModel_PrimitiveType_Head);
    }

    gdtf->CreateModel("Lamp", &modelLamp);
    modelLamp->SetPrimitiveType(eGdtfModel_PrimitiveType_Conventional);

    // --- Geometry tree: Base → [Yoke] → [Head] → Lamp ---
    const STransformMatrix identity = identityMatrix();

    IGdtfGeometryPtr geoBase;
    gdtf->CreateGeometry(eGdtfGeometry, "Base", modelBase, identity, &geoBase);

    IGdtfGeometryPtr geoYoke;
    if (hasPan)
        geoBase->CreateGeometry(eGdtfGeometryAxis, "Yoke",
                                modelYoke, identity, &geoYoke);

    IGdtfGeometryPtr geoHead;
    if (hasTilt)
    {
        IGdtfGeometry *parent = hasPan
            ? static_cast<IGdtfGeometry*>(geoYoke)
            : static_cast<IGdtfGeometry*>(geoBase);
        parent->CreateGeometry(eGdtfGeometryAxis, "Head",
                               modelHead, identity, &geoHead);
    }

    IGdtfGeometryPtr geoLamp;
    {
        IGdtfGeometry *parent;
        if (hasTilt)       parent = geoHead;
        else if (hasPan)   parent = geoYoke;
        else               parent = geoBase;
        parent->CreateGeometry(eGdtfGeometryLamp, "Lamp",
                               modelLamp, identity, &geoLamp);
        geoLamp->SetLuminousIntensity(1000.0);
        geoLamp->SetBeamAngle(beamAngle);
        geoLamp->SetFieldAngle(fieldAngle);
    }

    // --- DMX Mode ---
    IGdtfDmxModePtr dmxMode;
    gdtf->CreateDmxMode(modeName.toUtf8().constData(), &dmxMode);
    dmxMode->SetGeometry(geoBase);

    // --- DMX Channels — one GDTF DmxChannel per QLC 8-bit slot, collapsing
    //     MSB/LSB pairs of the same group into a single channel with fine. ---
    const QVector<QLCChannel*> channels = mode->channels();
    QSet<int> consumedLsb;

    for (int i = 0; i < channels.size(); ++i)
    {
        if (consumedLsb.contains(i))
            continue;

        QLCChannel *ch = channels.at(i);
        if (ch == nullptr)
            continue;

        const QLCChannel::Group group = ch->group();
        const QLCChannel::ControlByte cbyte = ch->controlByte();

        int coarseIdx = i;
        int fineIdx = -1;

        if (cbyte == QLCChannel::MSB)
        {
            const quint32 lsb = mode->channelNumber(group, QLCChannel::LSB);
            if (lsb != QLCChannel::invalid()
                && static_cast<int>(lsb) != i
                && lsb < static_cast<quint32>(channels.size()))
            {
                fineIdx = static_cast<int>(lsb);
                consumedLsb.insert(fineIdx);
            }
        }

        // Map QLC group → GDTF attribute + geometry node + physical range.
        IGdtfAttribute *attr = attrNoFeature;
        IGdtfGeometry  *geom = geoBase;
        double physFrom = 0.0;
        double physTo   = 0.0;

        switch (group)
        {
        case QLCChannel::Pan:
        {
            attr = attrPan;
            geom = hasPan ? static_cast<IGdtfGeometry*>(geoYoke)
                          : static_cast<IGdtfGeometry*>(geoBase);
            const double half = panRange / 2.0;
            physFrom = isMirror ?  half : -half;
            physTo   = isMirror ? -half :  half;
            break;
        }
        case QLCChannel::Tilt:
        {
            attr = attrTilt;
            geom = hasTilt ? static_cast<IGdtfGeometry*>(geoHead)
                           : static_cast<IGdtfGeometry*>(geoBase);
            const double half = tiltRange / 2.0;
            physFrom = isMirror ?  half : -half;
            physTo   = isMirror ? -half :  half;
            break;
        }
        case QLCChannel::Intensity:
            if (isDimmerPreset(ch->preset()))
            {
                attr = attrDimmer;
                geom = geoLamp;
                physFrom = 0.0;
                physTo   = 1.0;
            }
            break;
        default:
            break;
        }

        IGdtfDmxChannelPtr gdtfCh;
        if (dmxMode->CreateDmxChannel(geom, &gdtfCh) != kVCOMError_NoError)
            continue;

        gdtfCh->SetCoarse(coarseIdx + 1);
        if (fineIdx >= 0)
            gdtfCh->SetFine(fineIdx + 1);
        gdtfCh->SetGeometry(geom);

        IGdtfDmxLogicalChannelPtr logCh;
        if (gdtfCh->CreateLogicalChannel(attr, &logCh) != kVCOMError_NoError)
            continue;
        logCh->SetAttribute(attr);

        IGdtfDmxChannelFunctionPtr fn;
        const QString funcName = ch->name().isEmpty()
            ? QStringLiteral("Function") : ch->name();
        if (logCh->CreateDmxFunction(funcName.toUtf8().constData(), &fn)
            != kVCOMError_NoError)
            continue;
        fn->SetAttribute(attr);
        fn->SetPhysicalStart(physFrom);
        fn->SetPhysicalEnd(physTo);
    }

    // --- Finalize and grab the buffer ---
    err = gdtf->Close();
    if (err != kVCOMError_NoError)
        return fail(QStringLiteral("Close failed (err=%1)").arg(err));

    size_t bufferLen = 0;
    err = gdtf->GetBufferLength(bufferLen);
    if (err != kVCOMError_NoError || bufferLen == 0)
        return fail(QStringLiteral("GetBufferLength failed (err=%1, len=%2)")
                    .arg(err).arg(static_cast<qulonglong>(bufferLen)));

    QByteArray out(static_cast<int>(bufferLen), Qt::Uninitialized);
    err = gdtf->ToBuffer(out.data());
    if (err != kVCOMError_NoError)
        return fail(QStringLiteral("ToBuffer failed (err=%1)").arg(err));

    return out;
}
