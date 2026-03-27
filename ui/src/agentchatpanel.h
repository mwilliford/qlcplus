/*
  Q Light Controller Plus
  agentchatpanel.h

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

#ifndef AGENTCHATPANEL_H
#define AGENTCHATPANEL_H

#include <QWidget>
#include <QJsonArray>

#include "agentauthmanager.h"

class QTextBrowser;
class QLineEdit;
class QPushButton;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QSplitter;
class AgentConnection;
class Doc;

class AgentChatPanel : public QWidget
{
    Q_OBJECT

public:
    /** Agent generation states (client-side UX states). */
    enum GenerationState
    {
        Idle,       ///< Connected, not waiting for anything
        Pending,    ///< Message sent, waiting for first token
        Streaming,  ///< Receiving chat_agent tokens
        Compacting  ///< Server is compacting conversation history
    };

    /** Create and show the singleton panel window. */
    static void createAndShow(QWidget *parent, AgentConnection *connection, Doc *doc);

    /** Get the singleton instance (may be null). */
    static AgentChatPanel *instance() { return s_instance; }

    ~AgentChatPanel();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    AgentChatPanel(QWidget *parent, AgentConnection *connection, Doc *doc,
                   Qt::WindowFlags f = Qt::Widget);

private slots:
    void onSendClicked();
    void onConnectClicked();
    void onStateChanged(int state);
    void onChatToken(const QString &text);
    void onChatEnd();
    void onCommandExecuting(const QString &commandType);
    void onError(const QString &message);

    // Session slots
    void onSessionCreated(const QString &sessionId);
    void onSessionHistoryReceived(const QString &sessionId, const QJsonArray &messages, bool expired);
    void onSessionMetadataUpdated(const QString &sessionId);
    void onSessionItemDoubleClicked(QListWidgetItem *item);
    void onNewSessionClicked();
    void onToggleSidebar();
    void onSessionContextMenu(const QPoint &pos);

    // Session status & compaction slots
    void onSessionStatus(int estimatedTokens, int contextLimit);
    void onCompactingStarted();
    void onCompactingFinished();

    // Auth slots
    void onLoginRequired();
    void onAuthStateChanged(AgentAuthManager::AuthState state);
    void onConnectButtonContextMenu(const QPoint &pos);
    void onTokenPasteSubmit();

private:
    void setGenerationState(GenerationState state);
    void stopGeneration();
    void removeThinkingIndicator();
    void appendAligned(const QString &html, Qt::Alignment alignment);
    void appendUserMessage(const QString &text);
    void appendSystemMessage(const QString &text);
    void updateConnectButton();
    void scrollToBottom();
    void populateSidebar();
    void highlightActiveSession(const QString &sessionId);

    static AgentChatPanel *s_instance;

    AgentConnection *m_connection;
    Doc *m_doc;

    // Main layout
    QSplitter *m_splitter;

    // Sidebar
    QWidget *m_sidebarWidget;
    QListWidget *m_sessionList;
    QPushButton *m_newSessionButton;
    QPushButton *m_toggleSidebarButton;

    // Chat area
    QLabel *m_statusLabel;
    QLabel *m_contextLabel;     ///< Shows "6% context" token usage
    QPushButton *m_connectButton;
    QTextBrowser *m_chatView;
    QLineEdit *m_input;
    QPushButton *m_sendButton;

    // Token paste fallback
    QWidget *m_tokenPasteWidget;
    QLineEdit *m_tokenInput;
    QPushButton *m_tokenSubmitButton;

    GenerationState m_genState;
    QString m_thinkingText;     ///< Translated "Thinking..." for removal matching
    QString m_activeSessionId;
    QString m_pendingResumeSessionId;  ///< Session to resume after auto-connect
};

#endif // AGENTCHATPANEL_H
