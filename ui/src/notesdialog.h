/*
  Q Light Controller Plus
  notesdialog.h

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

#ifndef NOTESDIALOG_H
#define NOTESDIALOG_H

#include <QDialog>

#include "ui_notesdialog.h"

class NotesDialog : public QDialog, public Ui_NotesDialog
{
    Q_OBJECT
    Q_DISABLE_COPY(NotesDialog)

public:
    NotesDialog(QWidget *parent, const QString &objectName,
                const QString &userNote, const QString &agentNote);

    QString userNote() const;
    QString agentNote() const;

private slots:
    void slotClearAgentNote();
};

#endif // NOTESDIALOG_H
