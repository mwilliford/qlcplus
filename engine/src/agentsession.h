/*
  Q Light Controller Plus
  agentsession.h

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

#ifndef AGENTSESSION_H
#define AGENTSESSION_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#define KXMLAgentSessions  QStringLiteral("Sessions")
#define KXMLAgentSession   QStringLiteral("Session")
#define KXMLAgentSessionGoal QStringLiteral("Goal")

class AgentSession
{
public:
    QString sessionId;
    QString title;
    QStringList goals;
    QDateTime createdAt;

    bool loadXML(QXmlStreamReader &doc);
    bool saveXML(QXmlStreamWriter *doc) const;
};

#endif // AGENTSESSION_H
