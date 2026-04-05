/*
  Q Light Controller Plus
  calibrationmodel.cpp

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

#include "calibrationmodel.h"

CalibrationModel::CalibrationModel(QObject *parent)
    : QObject(parent)
    , m_hasState(false)
{
}

void CalibrationModel::updateFromJson(const QJsonObject &solveStateObj)
{
    if (solveStateObj.isEmpty())
    {
        clear();
        return;
    }

    rigmath::SolveState state;
    state.converged = solveStateObj["converged"].toBool(false);
    state.rms_residual = solveStateObj["rms_residual"].toDouble(0.0);

    // Parse transforms: { "1": { "pos": [x,y,z], "axis_angle": [ax,ay,az] } }
    QJsonObject transforms = solveStateObj["transforms"].toObject();
    for (auto it = transforms.begin(); it != transforms.end(); ++it)
    {
        QJsonObject t = it.value().toObject();
        QJsonArray pos = t["pos"].toArray();
        QJsonArray aa = t["axis_angle"].toArray();
        if (pos.size() == 3 && aa.size() == 3)
        {
            state.transforms[it.key().toStdString()] = {
                pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble(),
                aa[0].toDouble(), aa[1].toDouble(), aa[2].toDouble()
            };
        }
    }

    // Parse model_corrections: { "Model/Name": { "axis_angle": [ax,ay,az] } }
    QJsonObject corrections = solveStateObj["model_corrections"].toObject();
    for (auto it = corrections.begin(); it != corrections.end(); ++it)
    {
        QJsonObject c = it.value().toObject();
        QJsonArray aa = c["axis_angle"].toArray();
        if (aa.size() == 3)
        {
            state.model_corrections[it.key().toStdString()] = {
                aa[0].toDouble(), aa[1].toDouble(), aa[2].toDouble()
            };
        }
    }

    // Parse layout
    QJsonObject layout = solveStateObj["layout"].toObject();
    state.layout.total_params = layout["total_params"].toInt(0);

    QJsonObject fixtureOffset = layout["fixture_offset"].toObject();
    for (auto it = fixtureOffset.begin(); it != fixtureOffset.end(); ++it)
        state.layout.fixture_offset[it.key().toStdString()] = it.value().toInt();

    QJsonObject modelOffset = layout["model_offset"].toObject();
    for (auto it = modelOffset.begin(); it != modelOffset.end(); ++it)
        state.layout.model_offset[it.key().toStdString()] = it.value().toInt();

    // Parse covariance (flat row-major N*N array)
    QJsonArray covArray = solveStateObj["covariance"].toArray();
    state.covariance.reserve(covArray.size());
    for (const auto &v : covArray)
        state.covariance.push_back(v.toDouble());

    int expected = state.layout.total_params * state.layout.total_params;
    if ((int)state.covariance.size() != expected)
    {
        qWarning() << "[CalibrationModel] Covariance size mismatch:"
                    << state.covariance.size() << "vs expected" << expected;
        clear();
        return;
    }

    m_state = std::move(state);
    m_hasState = true;
    emit stateChanged();
}

void CalibrationModel::clear()
{
    m_hasState = false;
    m_state = rigmath::SolveState();
    emit stateChanged();
}

rigmath::PositionUncertainty CalibrationModel::fixtureUncertainty(const QString &fixtureId) const
{
    if (!m_hasState)
        return {};
    std::string id = fixtureId.toStdString();
    if (m_state.layout.fixture_offset.find(id) == m_state.layout.fixture_offset.end())
        return {};
    return rigmath::fixture_uncertainty(m_state, id);
}

rigmath::EllipsoidAxes CalibrationModel::errorEllipsoid(const QString &fixtureId) const
{
    if (!m_hasState)
        return {};
    std::string id = fixtureId.toStdString();
    if (m_state.layout.fixture_offset.find(id) == m_state.layout.fixture_offset.end())
        return {};
    return rigmath::error_ellipsoid(m_state, id);
}

rigmath::PositionUncertainty CalibrationModel::differentialUncertainty(
    const QString &fixtureA, const QString &fixtureB) const
{
    if (!m_hasState)
        return {};
    std::string a = fixtureA.toStdString();
    std::string b = fixtureB.toStdString();
    if (m_state.layout.fixture_offset.find(a) == m_state.layout.fixture_offset.end())
        return {};
    if (m_state.layout.fixture_offset.find(b) == m_state.layout.fixture_offset.end())
        return {};
    return rigmath::differential_uncertainty(m_state, a, b);
}

QStringList CalibrationModel::poorlyConstrained(double thresholdCm) const
{
    if (!m_hasState)
        return {};
    auto params = rigmath::poorly_constrained(m_state, thresholdCm);
    QStringList result;
    for (const auto &p : params)
        result.append(QString::fromStdString(p));
    return result;
}

rigmath::RigidTransform CalibrationModel::fixtureTransform(const QString &fixtureId) const
{
    if (!m_hasState)
        return rigmath::RigidTransform::identity();
    std::string id = fixtureId.toStdString();
    auto it = m_state.transforms.find(id);
    if (it == m_state.transforms.end())
        return rigmath::RigidTransform::identity();
    const auto &t = it->second;
    if (t.size() != 6)
        return rigmath::RigidTransform::identity();
    return rigmath::RigidTransform::from_pose(t[0], t[1], t[2], t[3], t[4], t[5]);
}

void CalibrationModel::fixtureMatrix4x4(const QString &fixtureId, double out[16]) const
{
    rigmath::RigidTransform t = fixtureTransform(fixtureId);
    t.to_4x4_column_major(out);
}
