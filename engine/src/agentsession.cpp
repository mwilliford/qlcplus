/*
  Q Light Controller Plus
  agentsession.cpp

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

#include "agentsession.h"

bool AgentSession::loadXML(QXmlStreamReader &doc)
{
    if (doc.name() != KXMLAgentSession)
        return false;

    QXmlStreamAttributes attrs = doc.attributes();
    sessionId = attrs.value("ID").toString();
    title = attrs.value("Title").toString();
    QString created = attrs.value("Created").toString();
    if (!created.isEmpty())
        createdAt = QDateTime::fromString(created, Qt::ISODate);

    while (doc.readNextStartElement())
    {
        if (doc.name() == KXMLAgentSessionGoal)
            goals.append(doc.readElementText());
        else
            doc.skipCurrentElement();
    }

    return !sessionId.isEmpty();
}

bool AgentSession::saveXML(QXmlStreamWriter *doc) const
{
    if (sessionId.isEmpty())
        return false;

    doc->writeStartElement(KXMLAgentSession);
    doc->writeAttribute("ID", sessionId);
    if (!title.isEmpty())
        doc->writeAttribute("Title", title);
    if (createdAt.isValid())
        doc->writeAttribute("Created", createdAt.toString(Qt::ISODate));

    for (const QString &goal : goals)
        doc->writeTextElement(KXMLAgentSessionGoal, goal);

    doc->writeEndElement();
    return true;
}
