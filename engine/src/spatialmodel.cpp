/*
  Q Light Controller Plus
  spatialmodel.cpp

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
#include <QJsonObject>
#include <QDebug>
#include <cmath>

#include "spatialmodel.h"
#include "monitorproperties.h"

#define KXMLQLCSpatialModel         "SpatialModel"
#define KXMLQLCSpatialFixture       "Fixture"
#define KXMLQLCSpatialPlane         "Plane"
#define KXMLQLCSpatialAttrID        "ID"
#define KXMLQLCSpatialAttrX         "X"
#define KXMLQLCSpatialAttrY         "Y"
#define KXMLQLCSpatialAttrZ         "Z"
#define KXMLQLCSpatialAttrRX        "RX"
#define KXMLQLCSpatialAttrRY        "RY"
#define KXMLQLCSpatialAttrRZ        "RZ"
#define KXMLQLCSpatialAttrName      "Name"
#define KXMLQLCSpatialAttrNX        "NX"
#define KXMLQLCSpatialAttrNY        "NY"
#define KXMLQLCSpatialAttrNZ        "NZ"
#define KXMLQLCSpatialAttrD         "D"

SpatialModel::SpatialModel(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Three-layer fixture transforms
// ---------------------------------------------------------------------------

void SpatialModel::setFixtureTransform(const QString &id,
                                       const rigmath::RigidTransform &t,
                                       Layer layer)
{
    FixtureEntry &entry = m_fixtures[id];

    switch (layer)
    {
    case Committed:
        entry.committed = t;
        // Writing committed clears proposals — user decision overrides
        entry.agentDerived.reset();
        entry.solverDerived.reset();
        // Default uncertainty for committed placements without solver data
        if (!m_hasSolverViz)
        {
            entry.viz.ellipsoidAxes[0] = 50.0;  // cm
            entry.viz.ellipsoidAxes[1] = 50.0;
            entry.viz.ellipsoidAxes[2] = 50.0;
            double identity[9] = {1,0,0, 0,1,0, 0,0,1};
            for (int i = 0; i < 9; i++)
                entry.viz.ellipsoidRot[i] = identity[i];
            entry.viz.quality = "moderate";
        }
        break;
    case AgentDerived:
        entry.agentDerived = t;
        break;
    case SolverDerived:
        entry.solverDerived = t;
        break;
    }

    emit fixtureTransformChanged(id);
}

std::optional<rigmath::RigidTransform> SpatialModel::committedTransform(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return std::nullopt;
    return it->committed;
}

std::optional<rigmath::RigidTransform> SpatialModel::agentDerivedTransform(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return std::nullopt;
    return it->agentDerived;
}

std::optional<rigmath::RigidTransform> SpatialModel::solverDerivedTransform(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return std::nullopt;
    return it->solverDerived;
}

rigmath::RigidTransform SpatialModel::renderTransform(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return rigmath::RigidTransform::identity();

    if (it->committed.has_value())
        return it->committed.value();
    if (it->agentDerived.has_value())
        return it->agentDerived.value();
    if (it->solverDerived.has_value())
        return it->solverDerived.value();
    return rigmath::RigidTransform::identity();
}

rigmath::RigidTransform SpatialModel::fixtureTransform(const QString &id) const
{
    return renderTransform(id);
}

void SpatialModel::fixtureMatrix4x4(const QString &id, double out[16]) const
{
    renderTransform(id).to_4x4_column_major(out);
}

bool SpatialModel::isNew(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return true;
    return !it->committed.has_value();
}

void SpatialModel::promoteTransform(const QString &id, Layer sourceLayer)
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return;

    std::optional<rigmath::RigidTransform> value;
    switch (sourceLayer)
    {
    case AgentDerived:
        value = it->agentDerived;
        break;
    case SolverDerived:
        value = it->solverDerived;
        break;
    case Committed:
        return;  // promoting committed to committed is a no-op
    }

    if (!value.has_value())
        return;

    it->committed = value;
    it->agentDerived.reset();
    it->solverDerived.reset();
    emit fixtureTransformChanged(id);
}

void SpatialModel::clearTransformLayer(const QString &id, Layer layer)
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return;

    switch (layer)
    {
    case Committed:
        it->committed.reset();
        break;
    case AgentDerived:
        it->agentDerived.reset();
        break;
    case SolverDerived:
        it->solverDerived.reset();
        break;
    }

    emit fixtureTransformChanged(id);
}

void SpatialModel::removeFixture(const QString &id)
{
    m_fixtures.remove(id);
}

QStringList SpatialModel::fixtureIds() const
{
    return m_fixtures.keys();
}

bool SpatialModel::hasFixture(const QString &id) const
{
    return m_fixtures.contains(id);
}

// ---------------------------------------------------------------------------
// mm/degree convenience accessors (for 2D view compat)
// Reads use renderTransform(). Writes go to committed layer.
// ---------------------------------------------------------------------------

QVector3D SpatialModel::fixturePositionMm(const QString &id) const
{
    rigmath::RigidTransform t = renderTransform(id);
    return QVector3D(float(t.pos[0] * 1000.0),
                     float(t.pos[1] * 1000.0),
                     float(t.pos[2] * 1000.0));
}

QVector3D SpatialModel::fixtureRotationDeg(const QString &id) const
{
    rigmath::RigidTransform t = renderTransform(id);

    // Decompose 3x3 rotation matrix (row-major) to ZYX Euler angles
    const double *r = t.rot;
    double rx, ry, rz;

    double sy = -r[6]; // -r20
    if (std::abs(sy) < 0.99999)
    {
        ry = asin(sy);
        double cy = cos(ry);
        rx = atan2(r[7] / cy, r[8] / cy);
        rz = atan2(r[3] / cy, r[0] / cy);
    }
    else
    {
        // Gimbal lock
        ry = sy > 0 ? M_PI / 2.0 : -M_PI / 2.0;
        rx = atan2(r[1], r[2]);
        rz = 0;
    }

    const double radToDeg = 180.0 / M_PI;
    return QVector3D(float(rx * radToDeg),
                     float(ry * radToDeg),
                     float(rz * radToDeg));
}

void SpatialModel::setFixturePositionMm(const QString &id, const QVector3D &posMm)
{
    // Preserve existing rotation from the best available transform
    rigmath::RigidTransform existing = renderTransform(id);
    existing.pos[0] = posMm.x() / 1000.0;
    existing.pos[1] = posMm.y() / 1000.0;
    existing.pos[2] = posMm.z() / 1000.0;
    setFixtureTransform(id, existing, Committed);
}

void SpatialModel::setFixtureRotationDeg(const QString &id, const QVector3D &rotDeg)
{
    // Preserve existing position from the best available transform
    rigmath::RigidTransform existing = renderTransform(id);

    const double degToRad = M_PI / 180.0;
    double rx = rotDeg.x() * degToRad;
    double ry = rotDeg.y() * degToRad;
    double rz = rotDeg.z() * degToRad;

    // Compose Euler ZYX rotation matrices
    rigmath::RigidTransform tx = rigmath::RigidTransform::from_axis_angle(rx, 0, 0);
    rigmath::RigidTransform ty = rigmath::RigidTransform::from_axis_angle(0, ry, 0);
    rigmath::RigidTransform tz = rigmath::RigidTransform::from_axis_angle(0, 0, rz);
    rigmath::RigidTransform combined = tz.compose(ty.compose(tx));

    // Keep position, replace rotation
    combined.pos[0] = existing.pos[0];
    combined.pos[1] = existing.pos[1];
    combined.pos[2] = existing.pos[2];

    setFixtureTransform(id, combined, Committed);
}

// ---------------------------------------------------------------------------
// Named planes
// ---------------------------------------------------------------------------

void SpatialModel::setPlanes(const QList<Plane> &planes)
{
    m_planes = planes;
    emit planesChanged();
}

QList<SpatialModel::Plane> SpatialModel::planes() const
{
    return m_planes;
}

// ---------------------------------------------------------------------------
// Ephemeral solver visualization
// ---------------------------------------------------------------------------

void SpatialModel::applySolverVisualization(const QJsonObject &msg)
{
    // Write solver transforms to solverDerived layer
    QJsonObject transforms = msg["transforms"].toObject();
    for (auto it = transforms.begin(); it != transforms.end(); ++it)
    {
        QJsonObject tj = it.value().toObject();
        QJsonArray pos = tj["pos"].toArray();
        QJsonArray aa = tj["axis_angle"].toArray();

        if (pos.size() == 3 && aa.size() == 3)
        {
            rigmath::RigidTransform t = rigmath::RigidTransform::from_pose(
                pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble(),
                aa[0].toDouble(), aa[1].toDouble(), aa[2].toDouble());

            FixtureEntry &entry = m_fixtures[it.key()];
            entry.solverDerived = t;
            emit fixtureTransformChanged(it.key());
        }
    }

    // Update ellipsoid viz data
    QJsonObject ellipsoids = msg["ellipsoids"].toObject();
    for (auto it = ellipsoids.begin(); it != ellipsoids.end(); ++it)
    {
        auto fIt = m_fixtures.find(it.key());
        if (fIt == m_fixtures.end())
            continue;

        QJsonObject e = it.value().toObject();
        FixtureViz &viz = fIt->viz;

        QJsonArray axes = e["axes"].toArray();
        if (axes.size() == 3)
        {
            viz.ellipsoidAxes[0] = axes[0].toDouble();
            viz.ellipsoidAxes[1] = axes[1].toDouble();
            viz.ellipsoidAxes[2] = axes[2].toDouble();
        }

        QJsonArray rot = e["rot"].toArray();
        if (rot.size() == 9)
        {
            for (int i = 0; i < 9; i++)
                viz.ellipsoidRot[i] = rot[i].toDouble();
        }

        viz.quality = e["quality"].toString();
    }

    m_rmsResidual = msg["rms_residual"].toDouble(0.0);
    m_converged = msg["converged"].toBool(false);

    m_poorlyConstrained.clear();
    QJsonArray pc = msg["poorly_constrained"].toArray();
    for (const auto &v : pc)
        m_poorlyConstrained.append(v.toString());

    m_hasSolverViz = true;
    emit solverVizChanged();
}

SpatialModel::FixtureViz SpatialModel::fixtureViz(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return {};
    return it->viz;
}

void SpatialModel::clearSolverViz()
{
    for (auto it = m_fixtures.begin(); it != m_fixtures.end(); ++it)
    {
        it->solverDerived.reset();
        it->viz = FixtureViz();
    }

    m_hasSolverViz = false;
    m_rmsResidual = 0.0;
    m_converged = false;
    m_poorlyConstrained.clear();
    emit solverVizChanged();
}

// ---------------------------------------------------------------------------
// XML Persistence — only committed transforms are persisted
// ---------------------------------------------------------------------------

bool SpatialModel::loadXML(QXmlStreamReader &reader)
{
    if (reader.name() != QLatin1String(KXMLQLCSpatialModel))
        return false;

    while (reader.readNextStartElement())
    {
        if (reader.name() == QLatin1String(KXMLQLCSpatialFixture))
        {
            QXmlStreamAttributes attrs = reader.attributes();
            QString id = attrs.value(KXMLQLCSpatialAttrID).toString();
            if (id.isEmpty())
            {
                reader.skipCurrentElement();
                continue;
            }

            double x = attrs.value(KXMLQLCSpatialAttrX).toDouble();
            double y = attrs.value(KXMLQLCSpatialAttrY).toDouble();
            double z = attrs.value(KXMLQLCSpatialAttrZ).toDouble();
            double rx = attrs.value(KXMLQLCSpatialAttrRX).toDouble();
            double ry = attrs.value(KXMLQLCSpatialAttrRY).toDouble();
            double rz = attrs.value(KXMLQLCSpatialAttrRZ).toDouble();

            FixtureEntry entry;
            entry.committed = rigmath::RigidTransform::from_pose(x, y, z, rx, ry, rz);
            m_fixtures[id] = entry;
            reader.skipCurrentElement();
        }
        else if (reader.name() == QLatin1String(KXMLQLCSpatialPlane))
        {
            QXmlStreamAttributes attrs = reader.attributes();
            Plane plane;
            plane.name = attrs.value(KXMLQLCSpatialAttrName).toString();
            plane.normal[0] = attrs.value(KXMLQLCSpatialAttrNX).toDouble();
            plane.normal[1] = attrs.value(KXMLQLCSpatialAttrNY).toDouble();
            plane.normal[2] = attrs.value(KXMLQLCSpatialAttrNZ).toDouble();
            plane.distance = attrs.value(KXMLQLCSpatialAttrD).toDouble();
            m_planes.append(plane);
            reader.skipCurrentElement();
        }
        else
        {
            reader.skipCurrentElement();
        }
    }

    return true;
}

void SpatialModel::saveXML(QXmlStreamWriter &writer) const
{
    // Only save fixtures with committed transforms + planes
    bool hasCommitted = false;
    for (auto it = m_fixtures.constBegin(); it != m_fixtures.constEnd(); ++it)
    {
        if (it->committed.has_value())
        {
            hasCommitted = true;
            break;
        }
    }

    if (!hasCommitted && m_planes.isEmpty())
        return;

    writer.writeStartElement(KXMLQLCSpatialModel);

    for (auto it = m_fixtures.constBegin(); it != m_fixtures.constEnd(); ++it)
    {
        if (!it->committed.has_value())
            continue;

        const rigmath::RigidTransform &t = it->committed.value();
        double ax, ay, az;
        t.get_axis_angle(ax, ay, az);

        writer.writeStartElement(KXMLQLCSpatialFixture);
        writer.writeAttribute(KXMLQLCSpatialAttrID, it.key());
        writer.writeAttribute(KXMLQLCSpatialAttrX, QString::number(t.pos[0], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrY, QString::number(t.pos[1], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrZ, QString::number(t.pos[2], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRX, QString::number(ax, 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRY, QString::number(ay, 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRZ, QString::number(az, 'g', 10));
        writer.writeEndElement();
    }

    for (const Plane &plane : m_planes)
    {
        writer.writeStartElement(KXMLQLCSpatialPlane);
        writer.writeAttribute(KXMLQLCSpatialAttrName, plane.name);
        writer.writeAttribute(KXMLQLCSpatialAttrNX, QString::number(plane.normal[0], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrNY, QString::number(plane.normal[1], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrNZ, QString::number(plane.normal[2], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrD, QString::number(plane.distance, 'g', 10));
        writer.writeEndElement();
    }

    writer.writeEndElement();
}

// ---------------------------------------------------------------------------
// Legacy migration
// ---------------------------------------------------------------------------

void SpatialModel::migrateFromMonitorProperties(MonitorProperties *monProps)
{
    if (!monProps)
        return;

    for (quint32 fid : monProps->fixtureItemsID())
    {
        QVector3D pos = monProps->fixturePosition(fid, 0, 0);  // mm, Z-up
        QVector3D rot = monProps->fixtureRotation(fid, 0, 0);  // degrees, Euler

        // Convert mm -> meters
        double x = pos.x() / 1000.0;
        double y = pos.y() / 1000.0;
        double z = pos.z() / 1000.0;

        // Convert Euler degrees -> axis-angle via ZYX composition
        double degToRad = M_PI / 180.0;
        double rx = rot.x() * degToRad;
        double ry = rot.y() * degToRad;
        double rz = rot.z() * degToRad;

        rigmath::RigidTransform tx = rigmath::RigidTransform::from_axis_angle(rx, 0, 0);
        rigmath::RigidTransform ty = rigmath::RigidTransform::from_axis_angle(0, ry, 0);
        rigmath::RigidTransform tz = rigmath::RigidTransform::from_axis_angle(0, 0, rz);
        rigmath::RigidTransform combined = tz.compose(ty.compose(tx));

        combined.pos[0] = x;
        combined.pos[1] = y;
        combined.pos[2] = z;

        QString id = QString::number(fid);
        FixtureEntry entry;
        entry.committed = combined;
        m_fixtures[id] = entry;
    }
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void SpatialModel::clear()
{
    m_fixtures.clear();
    m_planes.clear();
    clearSolverViz();
}
