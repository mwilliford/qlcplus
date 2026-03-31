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

    // --- v2 prompt quality tests ---
    // These test the agent's creative decision-making, not just graph mechanics.

    // Vague scene request → agent should ask about show-ready vs foundational
    void v2VagueSceneAsksClarification();

    // Explicit show-ready → scene includes dimmer + shutter + color
    void v2ShowReadySceneHasDimmerAndShutter();

    // Explicit foundational → scene only has color channel
    void v2FoundationalSceneColorOnly();

    // DMX correctness — color value in correct range
    void v2RedSceneCorrectColorValue();

    // DMX correctness — gobo by name, correct channel value
    void v2GoboByNameCorrectValue();

    // DMX correctness — nonexistent gobo name → agent lists available gobos
    void v2UnknownGoboListsOptions();

    // Chaser — per-step timing preserved
    void v2ChaserWithTiming();

    // Modify — change color replaces old value, doesn't duplicate
    void v2ModifyColorReplaces();

    // Blackout scene — vague request should ask which fixtures
    void v2VagueBlackoutAsksFixtures();

    // Blackout scene — explicit fixtures, verify shutter closed + dimmer off
    void v2ExplicitBlackoutCorrectValues();

    // Beat-synced chaser — vague should ask what functions to chase
    void v2VagueBeatChaserAsks();

    // Beat-synced chaser — explicit functions, verify tempo_type=Beats
    void v2ExplicitBeatChaserUsesBeats();

private:
    bool loadWorkspace(const QString &path);
    bool waitForState(int expectedState, int timeoutMs = 5000);
    bool hasRealLLM();

    Doc *m_doc;
    AgentConnection *m_conn;
    QString m_workspacePath;
};

#endif
