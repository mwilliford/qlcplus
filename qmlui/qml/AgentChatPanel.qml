/*
  Q Light Controller Plus
  AgentChatPanel.qml

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: agentPanel
    anchors.fill: parent
    color: "transparent"

    property string contextName: "AGENT"

    // Generation states (mirror C++ AgentChatPanel::GenerationState)
    readonly property int stateIdle: 0
    readonly property int statePending: 1
    readonly property int stateStreaming: 2
    readonly property int stateCompacting: 3

    property int genState: stateIdle
    property string streamingText: ""

    // Session tracking
    property string activeSessionId: ""
    property var sessionList: []
    property bool sidebarVisible: true

    // Connection state helpers
    readonly property bool isConnected: agentConnection.state === 4  // Connected
    readonly property bool isConnecting: agentConnection.state === 1 ||
                                         agentConnection.state === 2 ||
                                         agentConnection.state === 3

    function refreshSessions()
    {
        sessionList = agentConnection.getSessionList()
    }

    function submitToken()
    {
        var token = tokenInput.text.trim()
        if (token.length === 0) return
        agentConnection.authManager.handlePastedToken(token)
        tokenInput.text = ""
    }

    function sendMessage()
    {
        var text = inputField.text.trim()
        if (text.length === 0) return

        // If agent is generating, cancel first (implicit cancel)
        if (genState !== stateIdle)
        {
            agentConnection.sendCancel()
            // Commit partial streaming text before showing [Stopped]
            if (streamingText.length > 0)
            {
                chatHtml += formatMessage("assistant", streamingText)
                streamingText = ""
            }
            appendMessage("system", qsTr("[Stopped]"))
            genState = stateIdle
        }

        appendMessage("user", text)
        agentConnection.sendChatMessage(text)
        inputField.text = ""
        genState = statePending
        appendMessage("system", qsTr("Thinking..."))
    }

    function stateText()
    {
        switch (agentConnection.state)
        {
            case 0: return qsTr("Disconnected")
            case 1: return qsTr("Authenticating...")
            case 2: return qsTr("Connecting...")
            case 3: return qsTr("Waiting for sync...")
            case 4: return qsTr("Connected")
            default: return qsTr("Unknown")
        }
    }

    property string chatHtml: ""
    property int lastRole: -1  // track last role to merge streaming

    function formatMessage(role, text)
    {
        var escaped = text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
        if (role === "user")
            return "<p align='right'><font color='#ffffff'>" + escaped + "</font></p>"
        else if (role === "assistant")
            return "<p><font color='#90caf9'>" + escaped + "</font></p>"
        else if (role === "error")
            return "<p><font color='#ff6666'>" + escaped + "</font></p>"
        else
            return "<p><i><font color='#888888'>" + escaped + "</font></i></p>"
    }

    function appendMessage(role, text)
    {
        chatHtml += formatMessage(role, text)
        chatText.text = chatHtml
        lastRole = -1
    }

    function scrollToBottom()
    {
        Qt.callLater(function() {
            if (chatText.implicitHeight > chatView.height)
                chatView.contentY = chatText.implicitHeight - chatView.height
        })
    }

    function formatSessionDate(isoDate)
    {
        if (!isoDate) return ""
        var d = new Date(isoDate)
        var months = ["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"]
        return months[d.getMonth()] + " " + d.getDate()
    }

    // Auth manager signals — show token paste only when login is required
    Connections
    {
        target: agentConnection.authManager

        function onLoginRequired()
        {
            console.log("[AgentQML] loginRequired signal received")
            appendMessage("system", qsTr("Login required. Opening browser..."))
            tokenPasteRow.visible = true
            agentConnection.authManager.login()
        }

        function onAuthStateChanged(state)
        {
            console.log("[AgentQML] authStateChanged:", state)
            // Unknown=0, LoggedOut=1, Authenticating=2, Authenticated=3
            if (state === 3)
                tokenPasteRow.visible = false
        }
    }

    // Signal handlers for AgentConnection
    Connections
    {
        target: agentConnection

        function onStateChanged()
        {
            if (agentConnection.state === 4) // Connected
            {
                appendMessage("system", qsTr("Connected to agent server"))
                tokenPasteRow.visible = false
                refreshSessions()
            }
            else if (agentConnection.state === 0) // Disconnected
            {
                appendMessage("system", qsTr("Disconnected"))
                genState = stateIdle
            }
        }

        function onChatTokenReceived(text)
        {
            if (genState === stateIdle || genState === statePending)
            {
                // First token — start a new assistant message
                genState = stateStreaming
                streamingText = text
            }
            else if (genState === stateStreaming)
            {
                streamingText += text
            }
            // Rebuild the last assistant message in the HTML
            var escaped = streamingText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br>")
            chatText.text = chatHtml + "<p><font color='#90caf9'>" + escaped + "</font></p>"
            scrollToBottom()
        }

        function onChatStreamEnded()
        {
            if (streamingText.length > 0)
            {
                chatHtml += formatMessage("assistant", streamingText)
                chatText.text = chatHtml
            }
            genState = stateIdle
            streamingText = ""
        }

        function onCommandExecuting(commandType)
        {
            appendMessage("system", qsTr("Executing: %1").arg(commandType))
        }

        function onErrorOccurred(message)
        {
            appendMessage("error", message)
        }

        function onCompactingStarted()
        {
            genState = stateCompacting
            appendMessage("system", qsTr("Compacting conversation history..."))
        }

        function onCompactingFinished()
        {
            genState = stateIdle
            appendMessage("system", qsTr("Compaction complete"))
        }

        function onSessionCreated(sessionId)
        {
            activeSessionId = sessionId
            refreshSessions()
            appendMessage("system", qsTr("New session: %1").arg(sessionId.substring(0, 8)))
        }

        function onSessionMetadataUpdated(sessionId)
        {
            refreshSessions()
        }

        function onSessionHistoryReceived(sessionId, messages, expired)
        {
            if (expired)
            {
                appendMessage("system", qsTr("Session expired — starting fresh."))
                return
            }

            activeSessionId = sessionId
            chatHtml = ""

            for (var i = 0; i < messages.length; i++)
            {
                var msg = messages[i]
                var role = msg.role || "system"
                var text = msg.text || msg.content || ""
                if (text.length > 0)
                    chatHtml += formatMessage(role, text)
            }

            appendMessage("system", qsTr("Session resumed — %1 messages loaded.").arg(messages.length))
            scrollToBottom()
        }
    }

    Component.onCompleted: refreshSessions()

    // Main layout — sidebar + chat
    RowLayout
    {
        anchors.fill: parent
        spacing: 0

        // Session sidebar
        Rectangle
        {
            id: sidebar
            Layout.preferredWidth: sidebarVisible ? 160 : 0
            Layout.fillHeight: true
            color: UISettings.bgStrong
            visible: sidebarVisible
            clip: true

            ColumnLayout
            {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                // Sidebar header
                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    RobotoText
                    {
                        label: qsTr("Sessions")
                        fontSize: UISettings.textSizeDefault
                        Layout.fillWidth: true
                    }

                    IconButton
                    {
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        imgSource: "qrc:/add.svg"
                        tooltip: qsTr("New Session")
                        onClicked:
                        {
                            activeSessionId = ""
                            chatHtml = ""
                            chatText.text = ""
                            streamingText = ""
                            genState = stateIdle
                            appendMessage("system", qsTr("New session — type your first message to begin."))
                        }
                    }
                }

                // Session list
                ListView
                {
                    id: sessionListView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sessionList
                    spacing: 2

                    delegate: Rectangle
                    {
                        id: sessionDelegate
                        width: sessionListView.width
                        height: sessionTitle.height + sessionDate.height + 14
                        radius: 3
                        color: modelData.sessionId === activeSessionId
                               ? UISettings.highlight
                               : delegateMouseArea.containsMouse
                                 ? UISettings.bgMedium
                                 : "transparent"

                        Text
                        {
                            id: sessionTitle
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 6
                            text: modelData.title || modelData.sessionId.substring(0, 8)
                            font.family: UISettings.robotoFontName
                            font.pixelSize: UISettings.textSizeDefault * 0.9
                            color: UISettings.fgMain
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        Text
                        {
                            id: sessionDate
                            anchors.top: sessionTitle.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.topMargin: 2
                            text: formatSessionDate(modelData.createdAt)
                            font.family: UISettings.robotoFontName
                            font.pixelSize: UISettings.textSizeDefault * 0.75
                            color: "#888888"
                        }

                        MouseArea
                        {
                            id: delegateMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton

                            onClicked: (mouse) =>
                            {
                                if (mouse.button === Qt.RightButton)
                                {
                                    sessionContextMenu.sessionId = modelData.sessionId
                                    sessionContextMenu.popup()
                                }
                                else
                                {
                                    var sid = modelData.sessionId
                                    if (sid === activeSessionId) return

                                    if (isConnected)
                                    {
                                        appendMessage("system", qsTr("Resuming session..."))
                                        agentConnection.sendSessionResume(sid)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Separator line when sidebar is visible
        Rectangle
        {
            Layout.preferredWidth: sidebarVisible ? 1 : 0
            Layout.fillHeight: true
            color: UISettings.bgMedium
            visible: sidebarVisible
        }

        // Chat panel (right side)
        ColumnLayout
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 2
            spacing: 2

            // Status bar
            Rectangle
            {
                Layout.fillWidth: true
                Layout.preferredHeight: UISettings.iconSizeDefault
                Layout.minimumHeight: UISettings.iconSizeDefault
                color: UISettings.bgMedium
                radius: 3

                RowLayout
                {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 8

                    // Sidebar toggle (hamburger)
                    IconButton
                    {
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        imgSource: "qrc:/cuelist.svg"
                        tooltip: sidebarVisible ? qsTr("Hide Sessions") : qsTr("Show Sessions")
                        onClicked: sidebarVisible = !sidebarVisible
                    }

                    // Status indicator dot
                    Rectangle
                    {
                        width: 10; height: 10; radius: 5
                        color: isConnected ? "limegreen" : isConnecting ? "orange" : "gray"
                    }

                    RobotoText
                    {
                        label: stateText()
                        Layout.fillWidth: true
                        fontSize: UISettings.textSizeDefault * 0.9
                    }

                    GenericButton
                    {
                        id: connectButton
                        width: UISettings.bigItemHeight * 1.5
                        height: UISettings.iconSizeMedium
                        label: isConnected ? qsTr("Disconnect") : qsTr("Connect")
                        onClicked:
                        {
                            if (isConnected)
                                agentConnection.disconnectFromServer()
                            else
                                agentConnection.connectToServer()
                        }

                        MouseArea
                        {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: (mouse) => { connectMenu.popup() }
                        }

                        Menu
                        {
                            id: connectMenu

                            MenuItem
                            {
                                text: qsTr("Logout")
                                onTriggered:
                                {
                                    agentConnection.disconnectFromServer()
                                    agentConnection.authManager.logout()
                                    appendMessage("system", qsTr("Logged out — credentials cleared"))
                                }
                            }
                        }
                    }
                }
            }

            // Chat messages — single TextEdit for cross-message selection
            Flickable
            {
                id: chatView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: chatText.implicitHeight + 8
                contentWidth: width
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                TextEdit
                {
                    id: chatText
                    width: chatView.width - 20
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WordWrap
                    textFormat: TextEdit.RichText
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                    color: UISettings.fgMain
                    selectedTextColor: UISettings.fgMain
                    selectionColor: UISettings.highlight

                    onImplicitHeightChanged: scrollToBottom()
                }

                ScrollBar.vertical: ScrollBar { }
            }

            // Input area
            Rectangle
            {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(UISettings.iconSizeDefault * 1.2, inputField.implicitHeight + 8)
                Layout.minimumHeight: UISettings.iconSizeDefault * 1.2
                Layout.maximumHeight: UISettings.iconSizeDefault * 3
                color: UISettings.bgMedium
                radius: 3

                RowLayout
                {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4

                    TextArea
                    {
                        id: inputField
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: isConnected ? qsTr("Ask the AI agent...") : qsTr("Connect to start chatting")
                        enabled: isConnected
                        color: UISettings.fgMain
                        font.family: UISettings.robotoFontName
                        font.pixelSize: UISettings.textSizeDefault
                        wrapMode: TextArea.Wrap

                        background: Rectangle
                        {
                            color: UISettings.bgStrong
                            radius: 3
                        }

                        Keys.onReturnPressed: function(event)
                        {
                            if (!(event.modifiers & Qt.ShiftModifier))
                            {
                                sendMessage()
                                event.accepted = true
                            }
                        }
                    }

                    IconButton
                    {
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        imgSource: genState !== stateIdle ? "qrc:/stop.svg" : "qrc:/forward.svg"
                        tooltip: genState !== stateIdle ? qsTr("Cancel") : qsTr("Send")
                        enabled: isConnected
                        onClicked:
                        {
                            if (genState !== stateIdle)
                            {
                                agentConnection.sendCancel()
                                if (streamingText.length > 0)
                                {
                                    chatHtml += formatMessage("assistant", streamingText)
                                    streamingText = ""
                                }
                                appendMessage("system", qsTr("[Stopped]"))
                                genState = stateIdle
                            }
                            else
                                sendMessage()
                        }
                    }
                }
            }

            // Token paste — only shown when auth flow requires manual token entry
            Rectangle
            {
                id: tokenPasteRow
                Layout.fillWidth: true
                Layout.preferredHeight: UISettings.iconSizeDefault * 1.2
                Layout.minimumHeight: UISettings.iconSizeDefault * 1.2
                color: UISettings.bgMedium
                radius: 3
                visible: false

                RowLayout
                {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4

                    RobotoText
                    {
                        label: qsTr("Paste token:")
                        fontSize: UISettings.textSizeDefault * 0.9
                    }

                    TextArea
                    {
                        id: tokenInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "eyJ..."
                        color: UISettings.fgMain
                        font.family: UISettings.robotoFontName
                        font.pixelSize: UISettings.textSizeDefault

                        background: Rectangle
                        {
                            color: UISettings.bgStrong
                            radius: 3
                        }

                        Keys.onReturnPressed: submitToken()
                    }

                    GenericButton
                    {
                        width: UISettings.bigItemHeight
                        height: UISettings.iconSizeMedium
                        label: qsTr("Submit")
                        onClicked: submitToken()
                    }
                }
            }
        }
    }

    // Context menu for session delete
    Menu
    {
        id: sessionContextMenu
        property string sessionId: ""

        MenuItem
        {
            text: qsTr("Delete Session")
            onTriggered:
            {
                var sid = sessionContextMenu.sessionId
                if (sid === activeSessionId)
                {
                    activeSessionId = ""
                    chatHtml = ""
                    chatText.text = ""
                    appendMessage("system", qsTr("Session deleted. Type a message to start a new one."))
                }
                agentConnection.removeSession(sid)
                refreshSessions()
            }
        }
    }
}
