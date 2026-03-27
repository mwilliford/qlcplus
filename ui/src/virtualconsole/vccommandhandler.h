/*
  Q Light Controller Plus
  vccommandhandler.h

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

#ifndef VCCOMMANDHANDLER_H
#define VCCOMMANDHANDLER_H

#include <QJsonObject>

class VirtualConsole;
class Doc;

/**
 * Handle Virtual Console commands from the AI agent.
 * Lives in the UI layer so it can access VC widget classes.
 *
 * @param command  The command name (create_widget, modify_widget, etc.)
 * @param params   The full message JSON from the server
 * @param result   Output: populated with result data (widgetId, widget JSON, error, deltaAction)
 * @param vc       The VirtualConsole instance
 * @param doc      The Doc instance
 * @return true on success, false on error (result["error"] set)
 */
bool handleVCCommand(const QString &command, const QJsonObject &params,
                     QJsonObject &result, VirtualConsole *vc, Doc *doc);

#endif // VCCOMMANDHANDLER_H
