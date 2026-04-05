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
#define KXMLQLCSpatialAttrSource    "Source"
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
// Fixture transforms
// ---------------------------------------------------------------------------

void SpatialModel::setFixtureTransform(const QString &id,
                                       const rigmath::RigidTransform &t,
                                       Source source)
{
    FixtureEntry &entry = m_fixtures[id];
    entry.transform = t;
    entry.source = source;

    // For manual placements, assign default uncertainty (~50cm sphere, "moderate")
    // so ellipsoids are visible before the solver runs.
    // Solver results overwrite this with real covariance data.
    if (source == Manual && !m_hasSolverViz)
    {
        entry.viz.ellipsoidAxes[0] = 50.0;  // cm
        entry.viz.ellipsoidAxes[1] = 50.0;
        entry.viz.ellipsoidAxes[2] = 50.0;
        // Identity rotation (axis-aligned sphere)
        double identity[9] = {1,0,0, 0,1,0, 0,0,1};
        for (int i = 0; i < 9; i++)
            entry.viz.ellipsoidRot[i] = identity[i];
        entry.viz.quality = "moderate";
    }

    emit fixtureTransformChanged(id);
}

rigmath::RigidTransform SpatialModel::fixtureTransform(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return rigmath::RigidTransform::identity();
    return it->transform;
}

void SpatialModel::fixtureMatrix4x4(const QString &id, double out[16]) const
{
    fixtureTransform(id).to_4x4_column_major(out);
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

SpatialModel::Source SpatialModel::fixtureSource(const QString &id) const
{
    auto it = m_fixtures.find(id);
    if (it == m_fixtures.end())
        return Manual;
    return it->source;
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
    // Update transforms from solver
    QJsonObject transforms = msg["transforms"].toObject();
    for (auto it = transforms.begin(); it != transforms.end(); ++it)
    {
        QJsonObject t = it.value().toObject();
        QJsonArray pos = t["pos"].toArray();
        QJsonArray aa = t["axis_angle"].toArray();
        if (pos.size() == 3 && aa.size() == 3)
        {
            rigmath::RigidTransform rt = rigmath::RigidTransform::from_pose(
                pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble(),
                aa[0].toDouble(), aa[1].toDouble(), aa[2].toDouble()
            );
            FixtureEntry &entry = m_fixtures[it.key()];
            entry.transform = rt;
            entry.source = Solver;
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
        it->viz = FixtureViz();

    m_hasSolverViz = false;
    m_rmsResidual = 0.0;
    m_converged = false;
    m_poorlyConstrained.clear();
    emit solverVizChanged();
}

// ---------------------------------------------------------------------------
// XML Persistence
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
            entry.transform = rigmath::RigidTransform::from_pose(x, y, z, rx, ry, rz);

            QString sourceStr = attrs.value(KXMLQLCSpatialAttrSource).toString();
            entry.source = (sourceStr == QLatin1String("solver")) ? Solver : Manual;

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
    if (m_fixtures.isEmpty() && m_planes.isEmpty())
        return;

    writer.writeStartElement(KXMLQLCSpatialModel);

    for (auto it = m_fixtures.constBegin(); it != m_fixtures.constEnd(); ++it)
    {
        const FixtureEntry &entry = it.value();
        double ax, ay, az;
        entry.transform.get_axis_angle(ax, ay, az);

        writer.writeStartElement(KXMLQLCSpatialFixture);
        writer.writeAttribute(KXMLQLCSpatialAttrID, it.key());
        writer.writeAttribute(KXMLQLCSpatialAttrX, QString::number(entry.transform.pos[0], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrY, QString::number(entry.transform.pos[1], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrZ, QString::number(entry.transform.pos[2], 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRX, QString::number(ax, 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRY, QString::number(ay, 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrRZ, QString::number(az, 'g', 10));
        writer.writeAttribute(KXMLQLCSpatialAttrSource,
                              entry.source == Solver ? "solver" : "manual");
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

        // Convert mm → meters
        double x = pos.x() / 1000.0;
        double y = pos.y() / 1000.0;
        double z = pos.z() / 1000.0;

        // Convert Euler degrees → axis-angle radians
        // MonitorProperties Euler: rotX = pitch, rotY = yaw, rotZ = roll (degrees)
        // Simple approximation: for small rotations or single-axis, Euler ≈ axis-angle
        // For full accuracy, compose rotation matrices then decompose
        double degToRad = M_PI / 180.0;
        double rx = rot.x() * degToRad;
        double ry = rot.y() * degToRad;
        double rz = rot.z() * degToRad;

        // Build rotation via axis-angle composition for each Euler axis
        // R = Rz(rz) * Ry(ry) * Rx(rx) — standard ZYX Euler order
        rigmath::RigidTransform tx = rigmath::RigidTransform::from_axis_angle(rx, 0, 0);
        rigmath::RigidTransform ty = rigmath::RigidTransform::from_axis_angle(0, ry, 0);
        rigmath::RigidTransform tz = rigmath::RigidTransform::from_axis_angle(0, 0, rz);
        rigmath::RigidTransform combined = tz.compose(ty.compose(tx));

        // Set position
        combined.pos[0] = x;
        combined.pos[1] = y;
        combined.pos[2] = z;

        QString id = QString::number(fid);
        FixtureEntry entry;
        entry.transform = combined;
        entry.source = Manual;
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
