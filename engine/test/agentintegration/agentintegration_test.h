/*
  Q Light Controller Plus - Integration test
  agentintegration_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef AGENTINTEGRATION_TEST_H
#define AGENTINTEGRATION_TEST_H

#include <QObject>

class Doc;
class AgentConnection;

class AgentIntegration_Test : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Connection flow (no API key needed)
    void connectAndSync();
    void syncAckFields();
    void workspaceSyncIncludesAgentContext();
    void workspaceSyncIncludesVirtualConsole();

    // Chat (placeholder response)
    void chatPlaceholderResponse();

    // Workspace delta flow
    void fixtureAddedDelta();

    // Reconnection
    void reconnectSendsNewSync();

    // Session flow
    void sessionCreatedOnFirstChat();
    void sessionResumeExpired();

    // Agent behavior (requires API key — skips if unavailable)
    void updateAgentNoteViaChat();
    void agentNoteSurvivesReconnect();

    // Scene creation via agent (requires API key)
    void createSceneViaDelegation();
    void deleteSceneViaDelegation();

    // Sequence creation (requires API key)
    void createSequenceViaDelegation();

    // Chaser timing verification (requires API key)
    void createChaserWithTiming();

    // Workspace delta round-trip (verifies client sends correct format)
    void functionDeleteDeltaReachesServer();

    // Compaction (requires API key)
    void manualCompactRoundTrip();

    // modify_function set_values (requires API key)
    void modifySceneSetValuesViaAgent();

    // --- v2 intent-routed architecture tests ---
    // These set graphVersion="v2" in workspace_sync to test the v2 graph.

    // v2 session flow (no API key needed for connection)
    void v2SessionCreated();

    // v2 CONTROL intent — blackout (requires API key for Haiku router)
    void v2ControlBlackout();

    // v2 QUERY intent (requires API key for Haiku router + formatter)
    void v2QueryListFunctions();

    // v2 BUILD intent — scene creation (requires API key)
    void v2CreateScene();

    // v2 REFINE intent — modify existing scene (requires API key)
    void v2ModifyScene();

    // v2 BUILD intent — delete scene (requires API key)
    void v2DeleteScene();

    // v2 agent note round-trip (requires API key)
    void v2UpdateAgentNote();

    // v2 delta round-trip — verify deltas work with v2 sessions
    void v2DeltaRoundTrip();

    // v2 session resume
    void v2SessionResume();

private:
    bool loadWorkspace(const QString &path);
    bool waitForState(int expectedState, int timeoutMs = 5000);
    bool hasRealLLM();

    Doc *m_doc;
    AgentConnection *m_conn;
    QString m_workspacePath;
};

#endif
