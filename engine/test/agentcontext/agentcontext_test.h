/*
  Q Light Controller Plus - Unit test
  agentcontext_test.h

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#ifndef AGENTCONTEXT_TEST_H
#define AGENTCONTEXT_TEST_H

#include <QObject>

class Doc;

class AgentContext_Test : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // AgentContext basics
    void isEmpty();
    void saveEmpty();
    void saveAndLoadUserNote();
    void saveAndLoadAgentNote();
    void saveAndLoadBothNotes();
    void stripOnSave();

    // Fixture with AgentContext
    void fixtureSaveLoad();
    void fixtureNoContext();

    // Function subclasses with AgentContext
    void sceneSaveLoad();
    void chaserSaveLoad();
    void efxSaveLoad();
    void collectionSaveLoad();

    // Doc (workspace level) with AgentContext
    void docSaveLoad();

    // QLCFixtureDef with AgentContext
    void fixtureDefSaveLoad();

    // AgentConnection serialization
    void serializeWorkspaceSyncNoContext();
    void serializeWorkspaceSyncWithContext();

    // update_agent_note handler
    void updateAgentNoteFixture();
    void updateAgentNoteFunction();
    void updateAgentNoteWorkspace();
    void updateAgentNoteFixtureDef();
    void updateAgentNoteNotFound();

    // delete_function handler
    void deleteFunctionHandler();
    void deleteFunctionNotFound();

    // workspace_delta format
    void deltaFormatChangesArray();

    // Serialization completeness (Sample.qxw round-trip)
    void serializeSampleWorkspace();

    // Session data model
    void sessionSaveLoad();
    void sessionEmptyId();
    void sessionInAgentContext();
    void docSessionManagement();
    void docSessionPersistence();

    // Script function support
    void scriptSaveLoad();
    void createScriptHandler();
    void scriptSerialization();

    // DMX reading and running functions
    void getRunningFunctionsHandler();

    // modify_function: set_values vs add_values
    void modifySceneSetValues();
    void modifySceneAddValues();

    // AgentConnection state and auth
    void connectionStateEnum();
    void authStateEnum();
    void handlePastedJWT();
    void destructorSafety();

private:
    Doc *m_doc;
};

#endif
