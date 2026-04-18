/*
  Q Light Controller Plus
  gdtfwriter.h

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

#ifndef GDTFWRITER_H
#define GDTFWRITER_H

#include <QByteArray>
#include <QString>

class QLCFixtureDef;
class QLCFixtureMode;

/** @addtogroup engine Engine
 * @{
 */

/**
 * @brief Synthesizes a minimal GDTF archive in memory from a QXF fixture
 *        definition + mode, for use when MVR export needs a GDTF but the
 *        fixture was loaded from a QXF (no `.gdtf` source available).
 *
 * This is the MVR-3a scope ("attribute-only"): the produced GDTF contains a
 * minimal geometry tree (Base → [Yoke] → [Head/Scanner] → Lamp), a single DMX
 * mode whose channels are wired to GDTF attributes (Pan/Tilt/Dimmer/…), and
 * NO embedded mesh data (Models are primitive-only, no `<Files>`). Consumers
 * that need real geometry (grandMA3, Capture, BlenderDMX) will render the
 * fixture using GDTF primitives instead of a proper 3D body.
 *
 * MVR-3b (deferred) will extend this to embed `.glb` meshes converted from
 * QLC+'s bundled `.dae` files under `resources/meshes/fixtures/`.
 *
 * The output is a fully valid GDTF `.gdtf` zip archive (as bytes) that can
 * be passed directly to `IMediaRessourceVectorInterface::AddBufferToMvrFile`.
 */
class GDTFWriter
{
public:
    /**
     * Build a synthetic GDTF archive for the given fixture definition + mode.
     *
     * Geometry tree is derived from the mode's channel layout and physical
     * info:
     *   - Base (PrimitiveBase) — always present.
     *   - Yoke (PrimitiveYoke) — added if the mode has a Pan channel.
     *   - Head / Scanner — added if the mode has a Tilt channel; primitive
     *     type is `PrimitiveScanner` when `QLCPhysical::focusType() == "Mirror"`,
     *     otherwise `PrimitiveHead`.
     *   - Lamp (GeometryLamp, beam/field angles from `QLCPhysical::lensDegrees*`).
     *
     * DMX mode contains one `<DMXChannel>` per `QLCChannel` in the mode
     * (16-bit MSB/LSB pairs collapsed into a single channel with coarse/fine
     * offsets). Attribute mapping:
     *   - Group::Pan      → "Pan" (on Yoke)
     *   - Group::Tilt     → "Tilt" (on Head)
     *   - Group::Intensity → "Dimmer" (on Base)  [non-RGB intensity only]
     *   - Everything else → "NoFeature" (on Base)
     *
     * Physical ranges for Pan/Tilt come from `QLCPhysical::focusPanMax()` /
     * `focusTiltMax()`, centered on zero. Mirror scanners get inverted ranges
     * so physical-from > physical-to, matching the synthesized GDTFs we use
     * internally (see `synthesizeGDTFFromQXF` in gdtfkinematics.cpp).
     *
     * The fixture UUID is deterministic — a MD5 hash of
     * `manufacturer + model + mode.name` — so repeated round-trips through
     * MVR export produce identical UUIDs.
     *
     * @param def      Fixture definition (QXF-origin). Must be non-null.
     * @param mode     DMX mode to serialize. Must be non-null and belong to
     *                 `def`. Channel count and layout come from this mode.
     * @param outError If non-null, populated with a human-readable error
     *                 message on failure.
     * @return         GDTF zip bytes on success; empty QByteArray on failure.
     */
    static QByteArray writeSynthetic(const QLCFixtureDef *def,
                                     const QLCFixtureMode *mode,
                                     QString *outError = nullptr);
};

/** @} */

#endif // GDTFWRITER_H
