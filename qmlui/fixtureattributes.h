/*
  Q Light Controller Plus
  fixtureattributes.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef FIXTUREATTRIBUTES_H
#define FIXTUREATTRIBUTES_H

#include <QSet>
#include <QtGlobal>
#include <cstdint>
#include <functional>

class Fixture;

/**
 * @brief Thin semantic facade over a fixture's DMX channels.
 *
 * Wraps raw DMX writes (via the caller's writeDmx callback) in attribute-
 * level language: "set dimmer to 100%", "open shutter", "pan to 50%".
 * The facade handles channel discovery (QLCChannel groups, Shutter-open
 * capability lookup), fine-channel zeroing, and tracking which absolute
 * DMX addresses were touched so the caller can release them later.
 *
 * This is *not* a data-model abstraction — scenes, cues, and other
 * persistent state still store raw channel values. FixtureAttributes is
 * a write-side convenience for live overrides (SimpleDesk-layer).
 *
 * Typical use inside SpatialController / CalibrateController:
 *
 *     FixtureAttributes fa(fxi,
 *         [this](uint addr, uchar v){ emit focusDmxWrite(addr, v); },
 *         [this](uint addr){ emit focusDmxReset(addr); });
 *     if (fa.hasDimmer())  fa.setDimmerFull();
 *     if (fa.hasShutter()) fa.openShutter();
 *     // later, when releasing:
 *     fa.release();
 */
class FixtureAttributes
{
public:
    using WriteFn   = std::function<void(uint absAddr, uchar value)>;
    using ReleaseFn = std::function<void(uint absAddr)>;

    FixtureAttributes(Fixture *fxi, WriteFn write, ReleaseFn release = {});

    /** Whether the fixture actually has pan + tilt channels (QLC groups). */
    bool hasPanTilt() const;
    bool hasDimmer() const;
    bool hasShutter() const;

    /** Set pan MSB from 0..100 percent. LSB (if present) is zeroed. */
    void setPanPercent(double pct);
    void setTiltPercent(double pct);

    /** Set all Intensity-group channels to 255 (full). Includes fine
     *  channels if present. */
    void setDimmerFull();

    /** Open the shutter — picks the first capability with
     *  Preset=ShutterOpen (midpoint of its DMX range), falling back to
     *  a capability whose name starts with "Open", then 255 as a last
     *  resort. Applied to every Shutter-group channel. */
    void openShutter();

    /** Abs DMX addresses we wrote — caller tracks these to reset later. */
    const QSet<uint> &controlledChannels() const { return m_controlled; }

    /** Emit release for every channel we touched, via the ReleaseFn. */
    void release();

private:
    Fixture *m_fxi = nullptr;
    WriteFn m_write;
    ReleaseFn m_release;
    QSet<uint> m_controlled;
};

#endif // FIXTUREATTRIBUTES_H
