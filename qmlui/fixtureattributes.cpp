/*
  Q Light Controller Plus
  fixtureattributes.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#include "fixtureattributes.h"

#include "fixture.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "qlccapability.h"

#include <algorithm>

// Pick a DMX value that cleanly opens the shutter on this channel. Order:
//   1. A capability with Preset == ShutterOpen → midpoint of its range
//   2. A capability whose name starts with "Open" (case-insensitive)
//   3. 255 (conventional but wrong for some fixtures whose 255 means
//      "strobe" or "lamp off"; those should have ShutterOpen preset set)
static uchar findShutterOpenValue(const QLCChannel *ch)
{
    if (!ch) return 255;
    for (QLCCapability *cap : ch->capabilities())
    {
        if (cap->preset() == QLCCapability::ShutterOpen)
            return uchar((int(cap->min()) + int(cap->max())) / 2);
    }
    for (QLCCapability *cap : ch->capabilities())
    {
        if (cap->name().startsWith(QStringLiteral("Open"), Qt::CaseInsensitive))
            return uchar((int(cap->min()) + int(cap->max())) / 2);
    }
    return 255;
}

static inline double clampPct(double v)
{
    return v < 0.0 ? 0.0 : (v > 100.0 ? 100.0 : v);
}

FixtureAttributes::FixtureAttributes(Fixture *fxi, WriteFn write, ReleaseFn release)
    : m_fxi(fxi)
    , m_write(std::move(write))
    , m_release(std::move(release))
{
}

static inline uint absAddrFor(const Fixture *fxi, uint relCh)
{
    return fxi->universe() * 512U + fxi->address() + relCh;
}

bool FixtureAttributes::hasPanTilt() const
{
    if (!m_fxi) return false;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return false;
    return mode->channelNumber(QLCChannel::Pan,  QLCChannel::MSB) != QLCChannel::invalid()
        || mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB) != QLCChannel::invalid();
}

bool FixtureAttributes::hasDimmer() const
{
    if (!m_fxi) return false;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return false;
    for (const QLCChannel *ch : mode->channels())
        if (ch && ch->group() == QLCChannel::Intensity) return true;
    return false;
}

bool FixtureAttributes::hasShutter() const
{
    if (!m_fxi) return false;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return false;
    for (const QLCChannel *ch : mode->channels())
        if (ch && ch->group() == QLCChannel::Shutter) return true;
    return false;
}

void FixtureAttributes::setPanPercent(double pct)
{
    if (!m_fxi || !m_write) return;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return;
    const quint32 msb = mode->channelNumber(QLCChannel::Pan, QLCChannel::MSB);
    if (msb == QLCChannel::invalid()) return;
    pct = clampPct(pct);
    const uchar v = uchar(std::min(255, int(pct * 2.55 + 0.5)));
    const uint abs = absAddrFor(m_fxi, msb);
    m_write(abs, v);
    m_controlled.insert(abs);
    // Zero the fine channel if present so lingering LSB bits don't jitter.
    const quint32 lsb = mode->channelNumber(QLCChannel::Pan, QLCChannel::LSB);
    if (lsb != QLCChannel::invalid())
    {
        const uint absL = absAddrFor(m_fxi, lsb);
        m_write(absL, 0);
        m_controlled.insert(absL);
    }
}

void FixtureAttributes::setTiltPercent(double pct)
{
    if (!m_fxi || !m_write) return;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return;
    const quint32 msb = mode->channelNumber(QLCChannel::Tilt, QLCChannel::MSB);
    if (msb == QLCChannel::invalid()) return;
    pct = clampPct(pct);
    const uchar v = uchar(std::min(255, int(pct * 2.55 + 0.5)));
    const uint abs = absAddrFor(m_fxi, msb);
    m_write(abs, v);
    m_controlled.insert(abs);
    const quint32 lsb = mode->channelNumber(QLCChannel::Tilt, QLCChannel::LSB);
    if (lsb != QLCChannel::invalid())
    {
        const uint absL = absAddrFor(m_fxi, lsb);
        m_write(absL, 0);
        m_controlled.insert(absL);
    }
}

void FixtureAttributes::setDimmerFull()
{
    if (!m_fxi || !m_write) return;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return;
    const auto &channels = mode->channels();
    for (int i = 0; i < channels.size(); i++)
    {
        const QLCChannel *ch = channels.at(i);
        if (!ch || ch->group() != QLCChannel::Intensity) continue;
        const uint abs = absAddrFor(m_fxi, uint(i));
        m_write(abs, 255);
        m_controlled.insert(abs);
    }
}

void FixtureAttributes::openShutter()
{
    if (!m_fxi || !m_write) return;
    const QLCFixtureMode *mode = m_fxi->fixtureMode();
    if (!mode) return;
    const auto &channels = mode->channels();
    for (int i = 0; i < channels.size(); i++)
    {
        const QLCChannel *ch = channels.at(i);
        if (!ch || ch->group() != QLCChannel::Shutter) continue;
        const uint abs = absAddrFor(m_fxi, uint(i));
        m_write(abs, findShutterOpenValue(ch));
        m_controlled.insert(abs);
    }
}

void FixtureAttributes::release()
{
    if (m_release)
        for (uint a : m_controlled) m_release(a);
    m_controlled.clear();
}
