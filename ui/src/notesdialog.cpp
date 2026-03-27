/*
  Q Light Controller Plus
  notesdialog.cpp

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

#include "notesdialog.h"

NotesDialog::NotesDialog(QWidget *parent, const QString &objectName,
                         const QString &userNote, const QString &agentNote)
    : QDialog(parent)
{
    setupUi(this);
    setWindowTitle(tr("Notes — %1").arg(objectName));

    m_userNoteEdit->setPlainText(userNote);
    m_agentNoteEdit->setPlainText(agentNote);

    /* Hide agent note section when empty */
    bool hasAgentNote = !agentNote.isEmpty();
    m_agentNoteLabel->setVisible(hasAgentNote);
    m_agentNoteEdit->setVisible(hasAgentNote);
    m_clearAgentNoteButton->setVisible(hasAgentNote);

    connect(m_clearAgentNoteButton, SIGNAL(clicked()),
            this, SLOT(slotClearAgentNote()));
}

QString NotesDialog::userNote() const
{
    return m_userNoteEdit->toPlainText();
}

QString NotesDialog::agentNote() const
{
    return m_agentNoteEdit->toPlainText();
}

void NotesDialog::slotClearAgentNote()
{
    m_agentNoteEdit->clear();
    m_agentNoteLabel->setVisible(false);
    m_agentNoteEdit->setVisible(false);
    m_clearAgentNoteButton->setVisible(false);
}
