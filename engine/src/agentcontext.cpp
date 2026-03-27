/*
  Q Light Controller Plus
  agentcontext.cpp

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

#include "agentcontext.h"

bool AgentContext::s_stripOnSave = false;

bool AgentContext::loadXML(QXmlStreamReader &doc)
{
    if (doc.name() != KXMLAgentContext)
        return false;

    while (doc.readNextStartElement())
    {
        if (doc.name() == KXMLAgentUserNote)
            userNote = doc.readElementText();
        else if (doc.name() == KXMLAgentAgentNote)
            agentNote = doc.readElementText();
        else if (doc.name() == KXMLAgentSessions)
        {
            while (doc.readNextStartElement())
            {
                if (doc.name() == KXMLAgentSession)
                {
                    AgentSession session;
                    if (session.loadXML(doc))
                        sessions.append(session);
                }
                else
                    doc.skipCurrentElement();
            }
        }
        else
            doc.skipCurrentElement();
    }

    return true;
}

bool AgentContext::saveXML(QXmlStreamWriter *doc) const
{
    if (s_stripOnSave || isEmpty())
        return true;

    doc->writeStartElement(KXMLAgentContext);

    if (!userNote.isEmpty())
        doc->writeTextElement(KXMLAgentUserNote, userNote);
    if (!agentNote.isEmpty())
        doc->writeTextElement(KXMLAgentAgentNote, agentNote);

    if (!sessions.isEmpty())
    {
        doc->writeStartElement(KXMLAgentSessions);
        for (const AgentSession &session : sessions)
            session.saveXML(doc);
        doc->writeEndElement();
    }

    doc->writeEndElement();

    return true;
}
