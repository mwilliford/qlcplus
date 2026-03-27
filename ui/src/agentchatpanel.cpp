/*
  Q Light Controller Plus
  agentchatpanel.cpp

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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QSplitter>
#include <QListWidget>
#include <QMenu>
#include <QTextBlock>
#include <QJsonObject>

#include "agentchatpanel.h"
#include "agentconnection.h"
#include "agentsession.h"
#include "apputil.h"
#include "doc.h"

#define SETTINGS_AGENT_GEOMETRY "agentchatpanel/geometry"
#define SETTINGS_AGENT_SPLITTER "agentchatpanel/splitter"

AgentChatPanel *AgentChatPanel::s_instance = nullptr;

/*****************************************************************************
 * Initialization
 *****************************************************************************/

AgentChatPanel::AgentChatPanel(QWidget *parent, AgentConnection *connection,
                               Doc *doc, Qt::WindowFlags f)
    : QWidget(parent, f)
    , m_connection(connection)
    , m_doc(doc)
    , m_genState(Idle)
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    // Status bar (above splitter)
    QHBoxLayout *statusBar = new QHBoxLayout();

    m_toggleSidebarButton = new QPushButton(QStringLiteral("\u2630"), this);  // ☰ hamburger
    m_toggleSidebarButton->setFixedWidth(30);
    m_toggleSidebarButton->setToolTip(tr("Toggle session sidebar"));
    statusBar->addWidget(m_toggleSidebarButton);

    m_statusLabel = new QLabel(tr("Disconnected"), this);
    m_statusLabel->setStyleSheet("color: #888; font-weight: bold;");
    statusBar->addWidget(m_statusLabel);

    m_contextLabel = new QLabel("", this);
    m_contextLabel->setStyleSheet("color: #666; font-size: 11px;");
    statusBar->addWidget(m_contextLabel);
    statusBar->addStretch();

    m_connectButton = new QPushButton(tr("Connect"), this);
    m_connectButton->setFixedWidth(90);
    m_connectButton->setContextMenuPolicy(Qt::CustomContextMenu);
    statusBar->addWidget(m_connectButton);
    outerLayout->addLayout(statusBar);

    // Splitter: sidebar | chat
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // --- Sidebar ---
    m_sidebarWidget = new QWidget(m_splitter);
    QVBoxLayout *sidebarLayout = new QVBoxLayout(m_sidebarWidget);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(4);

    QLabel *sessionsLabel = new QLabel(tr("Sessions"), m_sidebarWidget);
    sessionsLabel->setStyleSheet("color: #ccc; font-weight: bold; padding: 4px;");
    sidebarLayout->addWidget(sessionsLabel);

    m_sessionList = new QListWidget(m_sidebarWidget);
    m_sessionList->setStyleSheet(
        "QListWidget {"
        "  background-color: #252526;"
        "  color: #ccc;"
        "  border: none;"
        "  font-size: 12px;"
        "}"
        "QListWidget::item {"
        "  padding: 6px 8px;"
        "  border-bottom: 1px solid #333;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #094771;"
        "}"
    );
    sidebarLayout->addWidget(m_sessionList, 1);

    m_newSessionButton = new QPushButton(tr("+ New Session"), m_sidebarWidget);
    sidebarLayout->addWidget(m_newSessionButton);

    m_splitter->addWidget(m_sidebarWidget);

    // --- Chat area ---
    QWidget *chatWidget = new QWidget(m_splitter);
    QVBoxLayout *chatLayout = new QVBoxLayout(chatWidget);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(4);

    m_chatView = new QTextBrowser(chatWidget);
    m_chatView->setReadOnly(true);
    m_chatView->setOpenExternalLinks(false);
    m_chatView->setStyleSheet(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  font-family: monospace;"
        "  font-size: 13px;"
        "  padding: 8px;"
        "}"
    );
    chatLayout->addWidget(m_chatView, 1);

    // Token paste bar (hidden by default — shown during login flow)
    m_tokenPasteWidget = new QWidget(chatWidget);
    QHBoxLayout *tokenBar = new QHBoxLayout(m_tokenPasteWidget);
    tokenBar->setContentsMargins(0, 0, 0, 0);
    QLabel *tokenLabel = new QLabel(tr("Paste token:"), m_tokenPasteWidget);
    tokenLabel->setStyleSheet("color: #888; font-size: 11px;");
    tokenBar->addWidget(tokenLabel);
    m_tokenInput = new QLineEdit(m_tokenPasteWidget);
    m_tokenInput->setPlaceholderText(tr("eyJ..."));
    tokenBar->addWidget(m_tokenInput, 1);
    m_tokenSubmitButton = new QPushButton(tr("Submit"), m_tokenPasteWidget);
    m_tokenSubmitButton->setFixedWidth(60);
    tokenBar->addWidget(m_tokenSubmitButton);
    m_tokenPasteWidget->hide();
    chatLayout->addWidget(m_tokenPasteWidget);

    // Input bar
    QHBoxLayout *inputBar = new QHBoxLayout();
    m_input = new QLineEdit(chatWidget);
    m_input->setPlaceholderText(tr("Type a message..."));
    m_input->setEnabled(false);
    inputBar->addWidget(m_input, 1);

    m_sendButton = new QPushButton(tr("Send"), chatWidget);
    m_sendButton->setFixedWidth(60);
    m_sendButton->setEnabled(false);
    inputBar->addWidget(m_sendButton);
    chatLayout->addLayout(inputBar);

    m_splitter->addWidget(chatWidget);

    // Splitter proportions: sidebar ~150px, chat gets the rest
    m_splitter->setSizes({150, 300});

    outerLayout->addWidget(m_splitter, 1);

    // Connections — existing
    connect(m_connectButton, &QPushButton::clicked,
            this, &AgentChatPanel::onConnectClicked);
    connect(m_connectButton, &QPushButton::customContextMenuRequested,
            this, &AgentChatPanel::onConnectButtonContextMenu);
    connect(m_sendButton, &QPushButton::clicked,
            this, &AgentChatPanel::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed,
            this, &AgentChatPanel::onSendClicked);

    connect(m_connection, &AgentConnection::stateChanged,
            this, &AgentChatPanel::onStateChanged);
    connect(m_connection, &AgentConnection::chatTokenReceived,
            this, &AgentChatPanel::onChatToken);
    connect(m_connection, &AgentConnection::chatStreamEnded,
            this, &AgentChatPanel::onChatEnd);
    connect(m_connection, &AgentConnection::commandExecuting,
            this, &AgentChatPanel::onCommandExecuting);
    connect(m_connection, &AgentConnection::errorOccurred,
            this, &AgentChatPanel::onError);
    connect(m_connection->authManager(), &AgentAuthManager::loginRequired,
            this, &AgentChatPanel::onLoginRequired);
    connect(m_connection->authManager(), &AgentAuthManager::authStateChanged,
            this, &AgentChatPanel::onAuthStateChanged);
    connect(m_connection->authManager(), &AgentAuthManager::authFailed,
            this, [this](const QString &error) {
        onError(error);
        // Re-show paste bar so user can retry
        m_tokenPasteWidget->show();
        m_tokenInput->clear();
        m_tokenInput->setFocus();
    });
    connect(m_tokenSubmitButton, &QPushButton::clicked,
            this, &AgentChatPanel::onTokenPasteSubmit);
    connect(m_tokenInput, &QLineEdit::returnPressed,
            this, &AgentChatPanel::onTokenPasteSubmit);

    // Connections — sessions
    connect(m_connection, &AgentConnection::sessionCreated,
            this, &AgentChatPanel::onSessionCreated);
    connect(m_connection, &AgentConnection::sessionHistoryReceived,
            this, &AgentChatPanel::onSessionHistoryReceived);
    connect(m_connection, &AgentConnection::sessionMetadataUpdated,
            this, &AgentChatPanel::onSessionMetadataUpdated);

    // Connections — session status & compaction
    connect(m_connection, &AgentConnection::sessionStatusReceived,
            this, &AgentChatPanel::onSessionStatus);
    connect(m_connection, &AgentConnection::compactingStarted,
            this, &AgentChatPanel::onCompactingStarted);
    connect(m_connection, &AgentConnection::compactingFinished,
            this, &AgentChatPanel::onCompactingFinished);

    connect(m_sessionList, &QListWidget::itemDoubleClicked,
            this, &AgentChatPanel::onSessionItemDoubleClicked);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionList, &QListWidget::customContextMenuRequested,
            this, &AgentChatPanel::onSessionContextMenu);
    connect(m_newSessionButton, &QPushButton::clicked,
            this, &AgentChatPanel::onNewSessionClicked);
    connect(m_toggleSidebarButton, &QPushButton::clicked,
            this, &AgentChatPanel::onToggleSidebar);

    // Populate sidebar from existing sessions
    populateSidebar();

    // Set initial button text based on current state
    updateConnectButton();
}

AgentChatPanel::~AgentChatPanel()
{
    QSettings settings;
    settings.setValue(SETTINGS_AGENT_GEOMETRY, saveGeometry());
    settings.setValue(SETTINGS_AGENT_SPLITTER, m_splitter->saveState());
    s_instance = nullptr;
}

void AgentChatPanel::createAndShow(QWidget *parent, AgentConnection *connection, Doc *doc)
{
    QWidget *window = nullptr;

    if (s_instance == nullptr)
    {
        s_instance = new AgentChatPanel(parent, connection, doc, Qt::Window);
        window = s_instance;

        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setWindowTitle(tr("AI Agent"));
        window->setContextMenuPolicy(Qt::CustomContextMenu);

        QSettings settings;
        QVariant var = settings.value(SETTINGS_AGENT_GEOMETRY);
        if (var.isValid())
        {
            window->restoreGeometry(var.toByteArray());
        }
        else
        {
            window->resize(600, 600);
        }

        QVariant splitterVar = settings.value(SETTINGS_AGENT_SPLITTER);
        if (splitterVar.isValid())
            s_instance->m_splitter->restoreState(splitterVar.toByteArray());

        AppUtil::ensureWidgetIsVisible(window);
    }
    else
    {
        window = s_instance;
    }

    window->show();
    window->raise();
}

void AgentChatPanel::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(SETTINGS_AGENT_GEOMETRY, saveGeometry());
    settings.setValue(SETTINGS_AGENT_SPLITTER, m_splitter->saveState());

    // Disconnect from server when window is closed
    if (m_connection->state() != AgentConnection::Disconnected)
        m_connection->disconnectFromServer();

    QWidget::closeEvent(event);
}

/*****************************************************************************
 * Slots
 *****************************************************************************/

void AgentChatPanel::onConnectClicked()
{
    switch (m_connection->state())
    {
    case AgentConnection::Disconnected:
        m_connection->connectToServer();
        break;
    case AgentConnection::Authenticating:
    case AgentConnection::Connecting:
    case AgentConnection::WaitingForSync:
    case AgentConnection::Connected:
        m_connection->disconnectFromServer();
        break;
    }
}

void AgentChatPanel::onLoginRequired()
{
    AgentAuthManager *auth = m_connection->authManager();
    if (auth->isAuthEnabled())
    {
        appendSystemMessage(tr("Login required. Opening browser..."));
        m_tokenPasteWidget->show();
        m_tokenInput->clear();
        auth->login();
    }
    else
    {
        appendSystemMessage(tr("No authentication configured. Set AGENT_API_TOKEN env var."));
    }
}

void AgentChatPanel::onAuthStateChanged(AgentAuthManager::AuthState state)
{
    if (state == AgentAuthManager::Authenticated)
    {
        m_tokenPasteWidget->hide();
        m_chatView->clear();
    }
    else if (state == AgentAuthManager::LoggedOut)
    {
        m_tokenPasteWidget->hide();
    }
    updateConnectButton();
}

void AgentChatPanel::onTokenPasteSubmit()
{
    QString input = m_tokenInput->text().trimmed();
    if (input.isEmpty())
        return;

    m_tokenInput->clear();
    m_tokenPasteWidget->hide();

    AgentAuthManager *auth = m_connection->authManager();
    auth->handlePastedToken(input);
}

void AgentChatPanel::onConnectButtonContextMenu(const QPoint &pos)
{
    AgentAuthManager *auth = m_connection->authManager();
    if (!auth->hasStoredCredentials())
        return;  // Nothing to logout from

    QMenu menu(this);
    QAction *logoutAction = menu.addAction(tr("Logout"));
    QAction *chosen = menu.exec(m_connectButton->mapToGlobal(pos));
    if (chosen == logoutAction)
    {
        m_connection->disconnectFromServer();
        auth->logout();
        m_chatView->clear();
        appendSystemMessage(tr("Logged out."));
    }
}

void AgentChatPanel::onSendClicked()
{
    // If generating and input is empty, this is a Stop click
    if (m_genState != Idle && m_input->text().trimmed().isEmpty())
    {
        stopGeneration();
        return;
    }

    // If generating and input has text, stop + send new message
    if (m_genState != Idle)
        stopGeneration();

    QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;

    appendUserMessage(text);
    m_connection->sendChatMessage(text);
    m_input->clear();
    setGenerationState(Pending);
}

void AgentChatPanel::onStateChanged(int state)
{
    switch (static_cast<AgentConnection::State>(state))
    {
    case AgentConnection::Disconnected:
        m_statusLabel->setText(tr("Disconnected"));
        m_statusLabel->setStyleSheet("color: #888; font-weight: bold;");
        m_contextLabel->setText("");
        m_input->setEnabled(false);
        m_sendButton->setEnabled(false);
        break;

    case AgentConnection::Authenticating:
        m_statusLabel->setText(tr("Logging in..."));
        m_statusLabel->setStyleSheet("color: #daa520; font-weight: bold;");
        m_input->setEnabled(false);
        m_sendButton->setEnabled(false);
        break;

    case AgentConnection::Connecting:
        m_statusLabel->setText(tr("Connecting..."));
        m_statusLabel->setStyleSheet("color: #daa520; font-weight: bold;");
        m_input->setEnabled(false);
        m_sendButton->setEnabled(false);
        break;

    case AgentConnection::WaitingForSync:
        m_statusLabel->setText(tr("Syncing..."));
        m_statusLabel->setStyleSheet("color: #daa520; font-weight: bold;");
        m_input->setEnabled(false);
        m_sendButton->setEnabled(false);
        break;

    case AgentConnection::Connected:
        m_statusLabel->setText(tr("Connected"));
        m_statusLabel->setStyleSheet("color: #4ec94e; font-weight: bold;");
        m_input->setEnabled(true);
        m_sendButton->setEnabled(true);
        m_input->setFocus();
        populateSidebar();

        // Resume pending session if we auto-connected for a session double-click
        if (!m_pendingResumeSessionId.isEmpty())
        {
            QString sid = m_pendingResumeSessionId;
            m_pendingResumeSessionId.clear();
            appendSystemMessage(tr("Resuming session..."));
            m_connection->sendSessionResume(sid);
        }
        break;
    }

    updateConnectButton();
}

void AgentChatPanel::updateConnectButton()
{
    switch (m_connection->state())
    {
    case AgentConnection::Disconnected:
        m_connectButton->setText(
            m_connection->authManager()->hasStoredCredentials() ? tr("Connect") : tr("Login"));
        break;
    case AgentConnection::Authenticating:
    case AgentConnection::Connecting:
    case AgentConnection::WaitingForSync:
        m_connectButton->setText(tr("Cancel"));
        break;
    case AgentConnection::Connected:
        m_connectButton->setText(tr("Disconnect"));
        break;
    }
}

void AgentChatPanel::onChatToken(const QString &text)
{
    if (m_genState != Streaming)
    {
        removeThinkingIndicator();
        appendAligned(
            QString("<span style='color: #d4d4d4;'>%1</span> ")
                .arg(tr("Agent:").toHtmlEscaped()),
            Qt::AlignLeft);
        setGenerationState(Streaming);
    }

    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);

    // Reset char format to default (white) — prevents inheriting orange from
    // [COMMAND] spans or other colored elements inserted between tokens.
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#d4d4d4"));
    cursor.setCharFormat(fmt);

    cursor.insertText(text);
    scrollToBottom();
}

void AgentChatPanel::onChatEnd()
{
    if (m_genState != Idle)
    {
        removeThinkingIndicator();
        setGenerationState(Idle);
        QTextCursor cursor = m_chatView->textCursor();
        cursor.movePosition(QTextCursor::End);
        cursor.insertText("\n");
        scrollToBottom();
    }
}

void AgentChatPanel::onCommandExecuting(const QString &commandType)
{
    appendAligned(
        QString("<span style='color: #ce9178;'>[COMMAND: %1]</span>")
        .arg(commandType.toHtmlEscaped()),
        Qt::AlignLeft);
    scrollToBottom();
}

void AgentChatPanel::onError(const QString &message)
{
    appendAligned(
        QString("<span style='color: #f44747;'>Error: %1</span>")
        .arg(message.toHtmlEscaped()),
        Qt::AlignLeft);
    scrollToBottom();
}

/*****************************************************************************
 * Session slots
 *****************************************************************************/

void AgentChatPanel::onSessionCreated(const QString &sessionId)
{
    m_activeSessionId = sessionId;
    populateSidebar();
    highlightActiveSession(sessionId);
}

void AgentChatPanel::onSessionHistoryReceived(const QString &sessionId,
                                               const QJsonArray &messages,
                                               bool expired)
{
    if (expired)
    {
        appendSystemMessage(tr("Session expired — starting fresh."));
        return;
    }

    m_activeSessionId = sessionId;
    m_chatView->clear();

    for (const QJsonValue &val : messages)
    {
        QJsonObject msg = val.toObject();
        QString role = msg["role"].toString();

        if (role == "user")
        {
            appendUserMessage(msg["text"].toString());
        }
        else if (role == "agent")
        {
            appendAligned(
                QString("<span style='color: #d4d4d4;'>%1</span> %2")
                .arg(tr("Agent:").toHtmlEscaped())
                .arg(msg["text"].toString().toHtmlEscaped()),
                Qt::AlignLeft);
        }
        else if (role == "agent_command")
        {
            appendAligned(
                QString("<span style='color: #ce9178;'>[COMMAND: %1]</span>")
                .arg(msg["command"].toString().toHtmlEscaped()),
                Qt::AlignLeft);
        }
    }

    appendSystemMessage(tr("Session resumed — %1 messages loaded.").arg(messages.size()));
    scrollToBottom();
    highlightActiveSession(sessionId);
}

void AgentChatPanel::onSessionMetadataUpdated(const QString &sessionId)
{
    populateSidebar();
    highlightActiveSession(m_activeSessionId);
}

void AgentChatPanel::onSessionItemDoubleClicked(QListWidgetItem *item)
{
    QString sessionId = item->data(Qt::UserRole).toString();
    if (sessionId.isEmpty() || sessionId == m_activeSessionId)
        return;

    if (m_connection->state() == AgentConnection::Connected)
    {
        appendSystemMessage(tr("Resuming session..."));
        m_connection->sendSessionResume(sessionId);
    }
    else if (m_connection->state() == AgentConnection::Disconnected)
    {
        // Auto-connect, then resume after connection is established
        m_pendingResumeSessionId = sessionId;
        appendSystemMessage(tr("Connecting to resume session..."));
        m_connection->connectToServer();
    }
}

void AgentChatPanel::onNewSessionClicked()
{
    m_activeSessionId.clear();
    m_chatView->clear();
    appendSystemMessage(tr("New session — type your first message to begin."));

    // Deselect all items in sidebar
    m_sessionList->clearSelection();
}

void AgentChatPanel::onToggleSidebar()
{
    bool visible = m_sidebarWidget->isVisible();
    m_sidebarWidget->setVisible(!visible);
}

void AgentChatPanel::onSessionContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_sessionList->itemAt(pos);
    if (item == nullptr)
        return;

    QString sessionId = item->data(Qt::UserRole).toString();
    if (sessionId.isEmpty())
        return;

    QMenu menu(this);
    QAction *deleteAction = menu.addAction(tr("Delete Session"));

    QAction *chosen = menu.exec(m_sessionList->mapToGlobal(pos));
    if (chosen == deleteAction)
    {
        // If deleting the active session, clear the chat
        if (sessionId == m_activeSessionId)
        {
            m_activeSessionId.clear();
            m_chatView->clear();
            appendSystemMessage(tr("Session deleted. Type a message to start a new one."));
        }

        m_doc->removeSession(sessionId);
        populateSidebar();
    }
}

/*****************************************************************************
 * Session status & compaction slots
 *****************************************************************************/

void AgentChatPanel::onSessionStatus(int estimatedTokens, int contextLimit)
{
    if (contextLimit > 0)
    {
        int pct = (estimatedTokens * 100) / contextLimit;
        QString color;
        if (pct > 75)
            color = "#f44747";
        else if (pct > 50)
            color = "#daa520";
        else
            color = "#666";
        m_contextLabel->setText(QString("%1% " + tr("context")).arg(pct));
        m_contextLabel->setStyleSheet(
            QString("color: %1; font-size: 11px;").arg(color));
    }
}

void AgentChatPanel::onCompactingStarted()
{
    setGenerationState(Compacting);
    appendSystemMessage(tr("Compacting conversation history..."));
}

void AgentChatPanel::onCompactingFinished()
{
    setGenerationState(Idle);
    m_input->setEnabled(true);
    m_sendButton->setEnabled(true);
    appendSystemMessage(tr("Conversation compacted."));
}

void AgentChatPanel::stopGeneration()
{
    if (m_genState == Idle)
        return;

    removeThinkingIndicator();
    setGenerationState(Idle);
    appendSystemMessage(tr("[Stopped]"));
    m_connection->sendCancel();
}

void AgentChatPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_genState != Idle)
    {
        stopGeneration();
        return;
    }
    QWidget::keyPressEvent(event);
}

/*****************************************************************************
 * Generation state
 *****************************************************************************/

void AgentChatPanel::setGenerationState(GenerationState state)
{
    m_genState = state;

    switch (state)
    {
    case Idle:
        m_statusLabel->setText(tr("Connected"));
        m_statusLabel->setStyleSheet("color: #4ec94e; font-weight: bold;");
        m_sendButton->setText(tr("Send"));
        m_sendButton->setStyleSheet("");
        break;

    case Pending:
        m_statusLabel->setText(tr("Thinking..."));
        m_statusLabel->setStyleSheet("color: #daa520; font-weight: bold;");
        m_sendButton->setText(tr("Stop"));
        m_sendButton->setStyleSheet("background-color: #c23b22; color: white; font-weight: bold;");
        // Ephemeral "Thinking..." in the chat — removed when first token arrives
        m_thinkingText = tr("Thinking...");
        appendAligned(
            QString("<span style='color: #888; font-style: italic;'>%1</span>")
                .arg(m_thinkingText.toHtmlEscaped()),
            Qt::AlignLeft);
        scrollToBottom();
        break;

    case Streaming:
        m_statusLabel->setText(tr("Connected"));
        m_statusLabel->setStyleSheet("color: #4ec94e; font-weight: bold;");
        // Stop button stays red during streaming
        m_sendButton->setText(tr("Stop"));
        m_sendButton->setStyleSheet("background-color: #c23b22; color: white; font-weight: bold;");
        break;

    case Compacting:
        m_statusLabel->setText(tr("Compacting..."));
        m_statusLabel->setStyleSheet("color: #daa520; font-weight: bold;");
        m_sendButton->setEnabled(false);
        m_input->setEnabled(false);
        break;
    }
}

void AgentChatPanel::removeThinkingIndicator()
{
    // Find and remove the "Thinking..." line from the chat.
    // We search for the italic text and remove the entire block.
    QTextCursor cursor(m_chatView->document());
    cursor.movePosition(QTextCursor::End);

    // Search backwards for the thinking indicator text
    QTextDocument *doc = m_chatView->document();
    for (QTextBlock block = doc->end().previous(); block.isValid(); block = block.previous())
    {
        if (block.text() == m_thinkingText)
        {
            cursor.setPosition(block.position());
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            // If this is the last block, select to end instead
            if (!cursor.hasSelection())
                cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            // Also remove the empty line left behind
            if (!cursor.atStart())
            {
                cursor.deletePreviousChar();  // remove trailing newline
            }
            break;
        }
    }
}

/*****************************************************************************
 * Helpers
 *****************************************************************************/

void AgentChatPanel::appendAligned(const QString &html, Qt::Alignment alignment)
{
    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (m_chatView->document()->isEmpty())
    {
        // First block — just set alignment on the existing empty block
        QTextBlockFormat blockFmt;
        blockFmt.setAlignment(alignment);
        cursor.setBlockFormat(blockFmt);
    }
    else
    {
        cursor.insertBlock();
        QTextBlockFormat blockFmt;
        blockFmt.setAlignment(alignment);
        cursor.setBlockFormat(blockFmt);
    }

    cursor.insertHtml(html);
}

void AgentChatPanel::appendUserMessage(const QString &text)
{
    QTextCursor cursor = m_chatView->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (!m_chatView->document()->isEmpty())
        cursor.insertBlock();

    QTextBlockFormat blockFmt;
    blockFmt.setAlignment(Qt::AlignRight);
    cursor.mergeBlockFormat(blockFmt);

    QTextCharFormat charFmt;
    charFmt.setForeground(QColor("#7ec8e3"));
    cursor.setCharFormat(charFmt);
    cursor.insertText(text);

    scrollToBottom();
}

void AgentChatPanel::appendSystemMessage(const QString &text)
{
    appendAligned(
        QString("<span style='color: #888;'>%1</span>")
            .arg(text.toHtmlEscaped()),
        Qt::AlignLeft);
    scrollToBottom();
}

void AgentChatPanel::scrollToBottom()
{
    QScrollBar *sb = m_chatView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AgentChatPanel::populateSidebar()
{
    m_sessionList->clear();

    const QList<AgentSession> &sessions = m_doc->sessions();
    for (int i = sessions.size() - 1; i >= 0; i--)  // newest first
    {
        const AgentSession &s = sessions[i];
        QString label = s.title.isEmpty() ? s.sessionId : s.title;
        if (s.createdAt.isValid())
            label += "\n" + s.createdAt.toString("MMM d, h:mm AP");

        QListWidgetItem *item = new QListWidgetItem(label, m_sessionList);
        item->setData(Qt::UserRole, s.sessionId);
    }
}

void AgentChatPanel::highlightActiveSession(const QString &sessionId)
{
    for (int i = 0; i < m_sessionList->count(); i++)
    {
        QListWidgetItem *item = m_sessionList->item(i);
        if (item->data(Qt::UserRole).toString() == sessionId)
        {
            m_sessionList->setCurrentItem(item);
            return;
        }
    }
}
