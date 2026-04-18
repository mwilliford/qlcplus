/*
  Q Light Controller Plus - Unit test
  agentcontext_test.cpp

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

#define protected public
#define private public
#include "agentcontext.h"
#include "agentconnection.h"
#include "qlcfile.h"
#include "qlcfixturedef.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "collection.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "scriptv4.h"
#include "qlcpalette.h"
#include "efx.h"
#include "doc.h"
#undef private
#undef protected

#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "../common/resource_paths.h"
#include "agentcontext_test.h"

void AgentContext_Test::initTestCase()
{
    m_doc = new Doc(this);

    // Load fixture cache using absolute path from CMake (FIXTUREDIR)
    // Falls back to relative INTERNAL_FIXTUREDIR for compatibility
#ifdef FIXTUREDIR
    QDir dir(FIXTUREDIR);
#else
    QDir dir(INTERNAL_FIXTUREDIR);
#endif
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    m_doc->fixtureDefCache()->loadMap(dir);
}

void AgentContext_Test::cleanupTestCase()
{
    delete m_doc;
}

void AgentContext_Test::cleanup()
{
    m_doc->clearContents();
    m_doc->m_agentContext = AgentContext();
    AgentContext::s_stripOnSave = false;
}

/*****************************************************************************
 * AgentContext basics
 *****************************************************************************/

void AgentContext_Test::isEmpty()
{
    AgentContext ctx;
    QVERIFY(ctx.isEmpty());

    ctx.userNote = "test";
    QVERIFY(!ctx.isEmpty());

    ctx.userNote.clear();
    ctx.agentNote = "test";
    QVERIFY(!ctx.isEmpty());
}

void AgentContext_Test::saveEmpty()
{
    AgentContext ctx;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    // Empty context should write nothing — verify no AgentContext element
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(buffer.readAll());
    QVERIFY(!xml.contains("AgentContext"));
}

void AgentContext_Test::saveAndLoadUserNote()
{
    AgentContext ctx;
    ctx.userNote = "Key light for dance floor";

    // Save
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    // Load
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // AgentContext

    AgentContext ctx2;
    QVERIFY(ctx2.loadXML(reader));
    QCOMPARE(ctx2.userNote, QString("Key light for dance floor"));
    QVERIFY(ctx2.agentNote.isEmpty());
}

void AgentContext_Test::saveAndLoadAgentNote()
{
    AgentContext ctx;
    ctx.agentNote = "Pan 45-120 covers dance floor";

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // AgentContext

    AgentContext ctx2;
    QVERIFY(ctx2.loadXML(reader));
    QVERIFY(ctx2.userNote.isEmpty());
    QCOMPARE(ctx2.agentNote, QString("Pan 45-120 covers dance floor"));
}

void AgentContext_Test::saveAndLoadBothNotes()
{
    AgentContext ctx;
    ctx.userNote = "My key light";
    ctx.agentNote = "Good for wash scenes";

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    AgentContext ctx2;
    QVERIFY(ctx2.loadXML(reader));
    QCOMPARE(ctx2.userNote, QString("My key light"));
    QCOMPARE(ctx2.agentNote, QString("Good for wash scenes"));
}

void AgentContext_Test::stripOnSave()
{
    AgentContext ctx;
    ctx.userNote = "Should not appear";
    ctx.agentNote = "Should not appear either";

    AgentContext::s_stripOnSave = true;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(buffer.readAll());
    QVERIFY(!xml.contains("AgentContext"));
    QVERIFY(!xml.contains("UserNote"));
    QVERIFY(!xml.contains("AgentNote"));
}

/*****************************************************************************
 * Fixture with AgentContext
 *****************************************************************************/

void AgentContext_Test::fixtureSaveLoad()
{
    Fixture fxi(m_doc);
    fxi.setID(42);
    fxi.setName("Test Scanner");
    fxi.setUniverse(0);
    fxi.setAddress(32);
    fxi.setUserNote("Aimed at mirror ball");
    fxi.setAgentNote("Pan range 45-120 covers dance floor");

    // Save
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    fxi.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    // Verify XML contains AgentContext
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(buffer.readAll());
    QVERIFY(xml.contains("AgentContext"));
    QVERIFY(xml.contains("UserNote"));
    QVERIFY(xml.contains("Aimed at mirror ball"));
    QVERIFY(xml.contains("AgentNote"));
    QVERIFY(xml.contains("Pan range 45-120"));
    buffer.close();

    // Load
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // Fixture

    Fixture fxi2(m_doc);
    QVERIFY(fxi2.loadXML(reader, m_doc, m_doc->fixtureDefCache()));
    QCOMPARE(fxi2.agentContext().userNote, QString("Aimed at mirror ball"));
    QCOMPARE(fxi2.agentContext().agentNote, QString("Pan range 45-120 covers dance floor"));
}

void AgentContext_Test::fixtureNoContext()
{
    // Fixture saved WITHOUT AgentContext should load fine (backward compat)
    Fixture fxi(m_doc);
    fxi.setID(1);
    fxi.setName("Plain Fixture");
    fxi.setUniverse(0);
    fxi.setAddress(0);
    // No setUserNote/setAgentNote

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    fxi.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    Fixture fxi2(m_doc);
    QVERIFY(fxi2.loadXML(reader, m_doc, m_doc->fixtureDefCache()));
    QVERIFY(fxi2.agentContext().isEmpty());
}

/*****************************************************************************
 * Function subclasses with AgentContext
 *****************************************************************************/

void AgentContext_Test::sceneSaveLoad()
{
    Scene *s = new Scene(m_doc);
    s->setName("Red Wash");
    s->setAgentNote("Component scene for collection 5");
    m_doc->addFunction(s);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    s->saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // Function

    Scene s2(m_doc);
    QVERIFY(s2.loadXML(reader));
    QCOMPARE(s2.agentContext().agentNote, QString("Component scene for collection 5"));
    QVERIFY(s2.agentContext().userNote.isEmpty());
}

void AgentContext_Test::chaserSaveLoad()
{
    Chaser *c = new Chaser(m_doc);
    c->setName("DJ Intro");
    c->setUserNote("Play this first");
    c->setAgentNote("6 steps, 16s each");
    m_doc->addFunction(c);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    c->saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    Chaser c2(m_doc);
    QVERIFY(c2.loadXML(reader));
    QCOMPARE(c2.agentContext().userNote, QString("Play this first"));
    QCOMPARE(c2.agentContext().agentNote, QString("6 steps, 16s each"));
}

void AgentContext_Test::efxSaveLoad()
{
    EFX *e = new EFX(m_doc);
    e->setName("Circle Pattern");
    e->setAgentNote("Both scanners, 5s period");
    m_doc->addFunction(e);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    e->saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    EFX e2(m_doc);
    QVERIFY(e2.loadXML(reader));
    QCOMPARE(e2.agentContext().agentNote, QString("Both scanners, 5s period"));
}

void AgentContext_Test::collectionSaveLoad()
{
    Collection *col = new Collection(m_doc);
    col->setName("Full Show");
    col->setUserNote("Main collection for weekend sets");
    m_doc->addFunction(col);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    col->saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    Collection col2(m_doc);
    QVERIFY(col2.loadXML(reader));
    QCOMPARE(col2.agentContext().userNote, QString("Main collection for weekend sets"));
}

/*****************************************************************************
 * Doc (workspace level) with AgentContext
 *****************************************************************************/

void AgentContext_Test::docSaveLoad()
{
    m_doc->setUserNote("Nightclub main room");
    m_doc->setAgentNote("3 fixtures, user prefers component scenes");

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    m_doc->saveXML(&writer);
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    // Verify XML
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(buffer.readAll());
    QVERIFY(xml.contains("AgentContext"));
    QVERIFY(xml.contains("Nightclub main room"));
    buffer.close();

    // Load into fresh doc
    m_doc->clearContents();
    m_doc->m_agentContext = AgentContext(); // reset

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Engine

    QVERIFY(m_doc->loadXML(reader));
    QCOMPARE(m_doc->agentContext().userNote, QString("Nightclub main room"));
    QCOMPARE(m_doc->agentContext().agentNote, QString("3 fixtures, user prefers component scenes"));
}

/*****************************************************************************
 * QLCFixtureDef with AgentContext
 *****************************************************************************/

void AgentContext_Test::fixtureDefSaveLoad()
{
    QLCFixtureDef def;
    def.setManufacturer("TestMfr");
    def.setModel("TestModel");
    def.setType(QLCFixtureDef::Scanner);
    def.setUserNote("Gobo wheel is slow");
    def.setAgentNote("Shutter ch9 must be 4+ for light output");

    // Add a channel and mode (upstream requires at least one mode for loadXML)
    QLCChannel *ch = new QLCChannel();
    ch->setName("Dimmer");
    ch->setGroup(QLCChannel::Intensity);
    def.addChannel(ch);

    QLCFixtureMode *mode = new QLCFixtureMode(&def);
    mode->setName("Default");
    mode->insertChannel(ch, 0);
    def.addMode(mode);

    // Save to temp file
    QString tmpPath = QDir::tempPath() + "/test_agentcontext.qxf";
    QFile::FileError err = def.saveXML(tmpPath);
    QCOMPARE(err, QFile::NoError);

    // Verify file contains AgentContext
    QFile file(tmpPath);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(xml.contains("AgentContext"));
    QVERIFY(xml.contains("Gobo wheel is slow"));
    QVERIFY(xml.contains("Shutter ch9 must be 4+"));

    // Load back
    QLCFixtureDef def2;
    err = def2.loadXML(tmpPath);
    QCOMPARE(err, QFile::NoError);
    QCOMPARE(def2.agentContext().userNote, QString("Gobo wheel is slow"));
    QCOMPARE(def2.agentContext().agentNote, QString("Shutter ch9 must be 4+ for light output"));

    QFile::remove(tmpPath);
}

/*****************************************************************************
 * AgentConnection serialization
 *****************************************************************************/

void AgentContext_Test::serializeWorkspaceSyncNoContext()
{
    AgentConnection conn(m_doc);
    QJsonObject sync = conn.buildWorkspaceSync();

    // No agent context set — agentContext field should be absent
    QVERIFY(!sync.contains("agentContext"));
}

void AgentContext_Test::serializeWorkspaceSyncWithContext()
{
    m_doc->setUserNote("Test workspace");
    m_doc->setAgentNote("Agent knows things");

    // Add a fixture with context
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("Test Fxi");
    fxi->setUniverse(0);
    fxi->setAddress(0);
    fxi->setChannels(4);
    fxi->setUserNote("Fixture user note");
    m_doc->addFixture(fxi);

    AgentConnection conn(m_doc);
    QJsonObject sync = conn.buildWorkspaceSync();

    // Workspace-level context
    QVERIFY(sync.contains("agentContext"));
    QJsonObject wsCtx = sync["agentContext"].toObject();
    QCOMPARE(wsCtx["userNote"].toString(), QString("Test workspace"));
    QCOMPARE(wsCtx["agentNote"].toString(), QString("Agent knows things"));

    // Fixture-level context
    QJsonArray fixtures = sync["fixtures"].toArray();
    QVERIFY(fixtures.size() >= 1);
    QJsonObject fxiJson = fixtures[0].toObject();
    QVERIFY(fxiJson.contains("agentContext"));
    QCOMPARE(fxiJson["agentContext"].toObject()["userNote"].toString(),
             QString("Fixture user note"));
}

/*****************************************************************************
 * update_agent_note handler
 *****************************************************************************/

void AgentContext_Test::updateAgentNoteFixture()
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("Scanner");
    fxi->setUniverse(0);
    fxi->setAddress(0);
    fxi->setChannels(12);
    m_doc->addFixture(fxi);
    quint32 fxiId = fxi->id();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-req-1";
    msg["agentNote"] = "Pan 45-120 covers dance floor";
    QJsonObject target;
    target["type"] = "fixture";
    target["id"] = (int)fxiId;
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    QCOMPARE(m_doc->fixture(fxiId)->agentContext().agentNote,
             QString("Pan 45-120 covers dance floor"));
}

void AgentContext_Test::updateAgentNoteFunction()
{
    Scene *s = new Scene(m_doc);
    s->setName("Test Scene");
    m_doc->addFunction(s);
    quint32 fnId = s->id();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-req-2";
    msg["agentNote"] = "Component scene for DJ intro";
    QJsonObject target;
    target["type"] = "function";
    target["id"] = (int)fnId;
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    QCOMPARE(m_doc->function(fnId)->agentContext().agentNote,
             QString("Component scene for DJ intro"));
}

void AgentContext_Test::updateAgentNoteWorkspace()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-req-3";
    msg["agentNote"] = "User prefers component scenes for collections";
    QJsonObject target;
    target["type"] = "workspace";
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    QCOMPARE(m_doc->agentContext().agentNote,
             QString("User prefers component scenes for collections"));
}

void AgentContext_Test::updateAgentNoteFixtureDef()
{
    // Load a fixture def from cache
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    if (def == nullptr)
    {
        QSKIP("Martin MAC250+ fixture def not in cache");
        return;
    }

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-req-4";
    msg["agentNote"] = "Color wheel has 14 positions";
    QJsonObject target;
    target["type"] = "fixtureDef";
    target["manufacturer"] = "Martin";
    target["model"] = "MAC250+";
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    QLCFixtureDef *updated = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QCOMPARE(updated->agentContext().agentNote, QString("Color wheel has 14 positions"));
}

void AgentContext_Test::updateAgentNoteNotFound()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-req-5";
    msg["agentNote"] = "This should fail";
    QJsonObject target;
    target["type"] = "fixture";
    target["id"] = 9999; // doesn't exist
    msg["target"] = target;

    // Should not crash — just sends error result
    conn.handleUpdateAgentNote(msg);

    // Verify nothing was set (fixture doesn't exist)
    QVERIFY(m_doc->fixture(9999) == nullptr);
}

/*****************************************************************************
 * delete_function handler
 *****************************************************************************/

void AgentContext_Test::deleteFunctionHandler()
{
    Scene *s = new Scene(m_doc);
    s->setName("Delete Me");
    m_doc->addFunction(s);
    quint32 fnId = s->id();

    QVERIFY(m_doc->function(fnId) != nullptr);

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_function";
    msg["requestId"] = "test-del-1";
    msg["functionId"] = (int)fnId;

    conn.handleDeleteFunction(msg);

    // Function should be gone from Doc
    QVERIFY(m_doc->function(fnId) == nullptr);
}

void AgentContext_Test::deleteFunctionNotFound()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_function";
    msg["requestId"] = "test-del-2";
    msg["functionId"] = 9999;

    // Should not crash — sends error result
    conn.handleDeleteFunction(msg);

    // Nothing to verify beyond no crash
}

/*****************************************************************************
 * workspace_delta format
 *****************************************************************************/

void AgentContext_Test::deltaFormatChangesArray()
{
    // Verify that onFunctionRemoved sends the correct changes array format
    // by checking the AgentConnection signal chain.
    // We can't easily test the WebSocket output in a unit test, but we can
    // verify that adding/removing functions on the Doc doesn't crash when
    // an AgentConnection is observing.

    AgentConnection conn(m_doc);
    // Note: conn is not connected, so sendDelta will be a no-op (state != Connected).
    // This just verifies the signal wiring doesn't crash.

    Scene *s = new Scene(m_doc);
    s->setName("Delta Test");
    m_doc->addFunction(s);
    quint32 fnId = s->id();

    // Remove — triggers functionRemoved signal → onFunctionRemoved → sendDelta (no-op)
    m_doc->deleteFunction(fnId);

    QVERIFY(m_doc->function(fnId) == nullptr);
}

/*****************************************************************************
 * Serialization completeness — load Sample.qxw, serialize, validate
 *****************************************************************************/

static bool loadWorkspaceIntoDoc(Doc *doc, const QString &path)
{
    QXmlStreamReader *reader = QLCFile::getXMLReader(path);
    if (!reader || reader->hasError())
        return false;

    // Skip to DTD
    while (!reader->atEnd())
    {
        if (reader->readNext() == QXmlStreamReader::DTD)
            break;
    }

    if (!reader->readNextStartElement())  // <Workspace>
    {
        QLCFile::releaseXMLReader(reader);
        return false;
    }

    doc->setWorkspacePath(QFileInfo(path).absolutePath());

    bool loaded = false;
    while (reader->readNextStartElement())
    {
        if (reader->name() == QStringLiteral("Engine"))
        {
            loaded = doc->loadXML(*reader);
            break;
        }
        else
        {
            reader->skipCurrentElement();
        }
    }

    QLCFile::releaseXMLReader(reader);
    return loaded;
}

void AgentContext_Test::serializeSampleWorkspace()
{
#ifdef SAMPLEDIR
    QString samplePath = QString(SAMPLEDIR) + "Sample.qxw";
#else
    QString samplePath = "../../../resources/samples/Sample.qxw";
#endif
    QVERIFY2(QFile::exists(samplePath), qPrintable("Sample.qxw not found at: " + samplePath));

    // Load Sample.qxw into a fresh Doc
    Doc *sampleDoc = new Doc(this);
#ifdef FIXTUREDIR
    QDir fdir(FIXTUREDIR);
#else
    QDir fdir(INTERNAL_FIXTUREDIR);
#endif
    fdir.setFilter(QDir::Files);
    fdir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    sampleDoc->fixtureDefCache()->loadMap(fdir);

    QVERIFY2(loadWorkspaceIntoDoc(sampleDoc, samplePath),
             "Failed to load Sample.qxw into Doc");

    // Build workspace_sync JSON
    AgentConnection conn(sampleDoc);
    QJsonObject sync = conn.buildWorkspaceSync();

    // --- Fixture validation ---
    QJsonArray fixtures = sync["fixtures"].toArray();
    QVERIFY2(fixtures.size() == 13,
             qPrintable(QString("Expected 13 fixtures, got %1").arg(fixtures.size())));

    // --- Function validation ---
    QJsonArray functions = sync["functions"].toArray();
    QVERIFY2(functions.size() >= 114,
             qPrintable(QString("Expected >= 114 functions, got %1").arg(functions.size())));

    int sceneCount = 0, chaserCount = 0, efxCount = 0, collectionCount = 0;
    int sequenceCount = 0, rgbMatrixCount = 0;
    int scenesWithValues = 0, chasersWithSteps = 0;
    int efxsWithFixtures = 0, collectionsWithMembers = 0;
    int functionsWithTempoType = 0;

    for (const QJsonValue &fv : functions)
    {
        QJsonObject fj = fv.toObject();
        QString type = fj["type"].toString();

        // All functions should have tempoType
        if (fj.contains("tempoType"))
            functionsWithTempoType++;

        if (type == "Scene")
        {
            sceneCount++;
            if (fj.contains("values") && fj["values"].toArray().size() > 0)
                scenesWithValues++;
        }
        else if (type == "Chaser")
        {
            chaserCount++;
            if (fj.contains("steps"))
            {
                QJsonArray steps = fj["steps"].toArray();
                if (steps.size() > 0)
                {
                    chasersWithSteps++;
                    // Verify step structure
                    QJsonObject step0 = steps[0].toObject();
                    QVERIFY2(step0.contains("functionId"),
                             qPrintable("Chaser step missing functionId in: " + fj["name"].toString()));
                    QVERIFY(step0.contains("fadeIn"));
                    QVERIFY(step0.contains("hold"));
                    QVERIFY(step0.contains("fadeOut"));
                }
            }
        }
        else if (type == "EFX")
        {
            efxCount++;
            if (fj.contains("efxFixtures"))
            {
                QJsonArray efxFix = fj["efxFixtures"].toArray();
                if (efxFix.size() > 0)
                {
                    efxsWithFixtures++;
                    // Verify EFX fixture structure
                    QJsonObject ef0 = efxFix[0].toObject();
                    QVERIFY2(ef0.contains("fixtureId"),
                             qPrintable("EFX fixture missing fixtureId in: " + fj["name"].toString()));
                    QVERIFY(ef0.contains("headIndex"));
                }
            }
            // Verify EFX params
            QVERIFY2(fj.contains("algorithm"),
                     qPrintable("EFX missing algorithm: " + fj["name"].toString()));
            QVERIFY(fj.contains("width"));
            QVERIFY(fj.contains("height"));
            QVERIFY(fj.contains("propagationMode"));
        }
        else if (type == "Collection")
        {
            collectionCount++;
            if (fj.contains("memberFunctionIds") && fj["memberFunctionIds"].toArray().size() > 0)
                collectionsWithMembers++;
        }
        else if (type == "Sequence")
        {
            sequenceCount++;
            QVERIFY2(fj.contains("boundSceneId"),
                     qPrintable("Sequence missing boundSceneId: " + fj["name"].toString()));
            QVERIFY(fj.contains("steps"));
        }
        else if (type == "RGBMatrix")
        {
            rgbMatrixCount++;
            QVERIFY2(fj.contains("fixtureGroupId"),
                     qPrintable("RGBMatrix missing fixtureGroupId: " + fj["name"].toString()));
        }
    }

    // Sample.qxw should have: 96 scenes, 13 chasers, 5 EFXs
    QVERIFY2(sceneCount >= 96,
             qPrintable(QString("Expected >= 96 scenes, got %1").arg(sceneCount)));
    QVERIFY2(chaserCount >= 13,
             qPrintable(QString("Expected >= 13 chasers, got %1").arg(chaserCount)));
    QVERIFY2(efxCount >= 5,
             qPrintable(QString("Expected >= 5 EFXs, got %1").arg(efxCount)));

    // Completeness: all scenes with channel values should have values serialized
    QVERIFY2(scenesWithValues > 0, "No scenes have values serialized");

    // All chasers should have steps
    QVERIFY2(chasersWithSteps == chaserCount,
             qPrintable(QString("Only %1/%2 chasers have steps").arg(chasersWithSteps).arg(chaserCount)));

    // All EFXs should have fixtures
    QVERIFY2(efxsWithFixtures == efxCount,
             qPrintable(QString("Only %1/%2 EFXs have fixtures").arg(efxsWithFixtures).arg(efxCount)));

    // All functions should have tempoType
    QVERIFY2(functionsWithTempoType == functions.size(),
             qPrintable(QString("Only %1/%2 functions have tempoType").arg(functionsWithTempoType).arg(functions.size())));

    // --- universeCount should reflect actual count ---
    QVERIFY(sync.contains("universeCount"));
    QVERIFY(sync["universeCount"].toInt() > 0);

    // --- Write JSON to file for server test fixtures ---
    QJsonDocument jsonDoc(sync);
    QByteArray jsonBytes = jsonDoc.toJson(QJsonDocument::Indented);

#ifdef SAMPLEDIR
    // Write next to the sample file for easy reference
    QString outPath = QString(SAMPLEDIR) + "../../../ai-qlcplus-server/tests/fixtures/sample_workspace.json";
#else
    QString outPath = "sample_workspace.json";
#endif
    QFile outFile(outPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        outFile.write(jsonBytes);
        outFile.close();
        qDebug() << "Wrote sample_workspace.json:" << outFile.fileName() << "(" << jsonBytes.size() << "bytes)";
    }

    // Summary
    qDebug() << "Serialization summary:";
    qDebug() << "  Fixtures:" << fixtures.size();
    qDebug() << "  Functions:" << functions.size()
             << "(Scenes:" << sceneCount
             << "Chasers:" << chaserCount
             << "EFX:" << efxCount
             << "Collections:" << collectionCount
             << "Sequences:" << sequenceCount
             << "RGBMatrix:" << rgbMatrixCount << ")";
    qDebug() << "  Scenes with values:" << scenesWithValues;
    qDebug() << "  Chasers with steps:" << chasersWithSteps;
    qDebug() << "  EFXs with fixtures:" << efxsWithFixtures;

    delete sampleDoc;
}

/*****************************************************************************
 * Session tests
 *****************************************************************************/

void AgentContext_Test::sessionSaveLoad()
{
    AgentSession session;
    session.sessionId = "ses_abc123";
    session.title = "Color wash scenes";
    session.goals << "Build 6 color washes" << "Test with DJ intro";
    session.createdAt = QDateTime(QDate(2026, 3, 21), QTime(10, 30, 0));

    // Save
    QBuffer buf;
    buf.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xml(&buf);
    xml.writeStartDocument();
    QVERIFY(session.saveXML(&xml));
    xml.writeEndDocument();

    // Load
    buf.close();
    buf.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buf);
    reader.readNextStartElement(); // Session
    AgentSession loaded;
    QVERIFY(loaded.loadXML(reader));

    QCOMPARE(loaded.sessionId, QString("ses_abc123"));
    QCOMPARE(loaded.title, QString("Color wash scenes"));
    QCOMPARE(loaded.goals.size(), 2);
    QCOMPARE(loaded.goals[0], QString("Build 6 color washes"));
    QCOMPARE(loaded.goals[1], QString("Test with DJ intro"));
    QVERIFY(loaded.createdAt.isValid());
}

void AgentContext_Test::sessionEmptyId()
{
    AgentSession session;
    // No sessionId set

    QBuffer buf;
    buf.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xml(&buf);
    xml.writeStartDocument();
    QVERIFY(!session.saveXML(&xml)); // Should return false
}

void AgentContext_Test::sessionInAgentContext()
{
    // Save AgentContext with sessions
    AgentContext ctx;
    ctx.userNote = "Test workspace";
    AgentSession s1;
    s1.sessionId = "ses_111";
    s1.title = "Session One";
    AgentSession s2;
    s2.sessionId = "ses_222";
    s2.title = "Session Two";
    ctx.sessions << s1 << s2;

    QBuffer buf;
    buf.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xml(&buf);
    xml.writeStartDocument();
    ctx.saveXML(&xml);
    xml.writeEndDocument();

    // Load
    buf.close();
    buf.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buf);
    reader.readNextStartElement(); // AgentContext
    AgentContext loaded;
    QVERIFY(loaded.loadXML(reader));

    QCOMPARE(loaded.userNote, QString("Test workspace"));
    QCOMPARE(loaded.sessions.size(), 2);
    QCOMPARE(loaded.sessions[0].sessionId, QString("ses_111"));
    QCOMPARE(loaded.sessions[1].sessionId, QString("ses_222"));
}

void AgentContext_Test::docSessionManagement()
{
    AgentSession s1;
    s1.sessionId = "ses_aaa";
    s1.title = "First";

    AgentSession s2;
    s2.sessionId = "ses_bbb";
    s2.title = "Second";

    m_doc->addSession(s1);
    m_doc->addSession(s2);
    QCOMPARE(m_doc->sessions().size(), 2);

    // Update
    m_doc->updateSession("ses_aaa", "Updated First", QStringList() << "Goal 1");
    QCOMPARE(m_doc->sessions()[0].title, QString("Updated First"));
    QCOMPARE(m_doc->sessions()[0].goals.size(), 1);

    // Remove
    m_doc->removeSession("ses_bbb");
    QCOMPARE(m_doc->sessions().size(), 1);
    QCOMPARE(m_doc->sessions()[0].sessionId, QString("ses_aaa"));
}

void AgentContext_Test::docSessionPersistence()
{
    // Add a session and workspace note
    m_doc->setUserNote("Workspace with sessions");
    AgentSession session;
    session.sessionId = "ses_persist";
    session.title = "Persist Test";
    session.goals << "Verify XML round-trip";
    session.createdAt = QDateTime::currentDateTime();
    m_doc->addSession(session);

    // Save the doc to a buffer
    QBuffer buf;
    buf.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xml(&buf);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("Workspace");
    m_doc->saveXML(&xml);
    xml.writeEndElement();
    xml.writeEndDocument();

    // Clear and reload
    m_doc->clearContents();
    QCOMPARE(m_doc->sessions().size(), 0);

    buf.close();
    buf.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buf);

    // Navigate to the Engine element inside Workspace
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("Engine"))
        {
            m_doc->loadXML(reader);
            break;
        }
    }

    // Verify sessions survived the round-trip
    QCOMPARE(m_doc->agentContext().userNote, QString("Workspace with sessions"));
    QCOMPARE(m_doc->sessions().size(), 1);
    QCOMPARE(m_doc->sessions()[0].sessionId, QString("ses_persist"));
    QCOMPARE(m_doc->sessions()[0].title, QString("Persist Test"));
    QCOMPARE(m_doc->sessions()[0].goals.size(), 1);
}

/*****************************************************************************
 * Script function support
 *****************************************************************************/

void AgentContext_Test::scriptSaveLoad()
{
    Script *s = new Script(m_doc);
    s->setName("Test Script");
    s->setData("startfunction:0\nwait:1000\nstopfunction:0\n");
    s->setAgentNote("Simple start/wait/stop");
    m_doc->addFunction(s);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    s->saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // Function

    Script s2(m_doc);
    QVERIFY(s2.loadXML(reader));
    // Note: name is loaded by Doc layer, not by Script::loadXML
    QCOMPARE(s2.data(), s->data());
    QCOMPARE(s2.agentContext().agentNote, QString("Simple start/wait/stop"));
}

void AgentContext_Test::createScriptHandler()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "create_script";
    msg["requestId"] = "test-script-1";
    msg["name"] = "Flash Loop";
    msg["data"] = "startfunction:0\nwait:500\nstopfunction:0\nwait:500";
    msg["runOrder"] = "Loop";

    int countBefore = m_doc->functions().size();
    conn.handleCreateScript(msg);
    int countAfter = m_doc->functions().size();

    QCOMPARE(countAfter, countBefore + 1);

    // Find the new Script
    Function *fn = nullptr;
    for (Function *f : m_doc->functions())
    {
        if (f && f->name() == "Flash Loop")
        {
            fn = f;
            break;
        }
    }
    QVERIFY(fn != nullptr);
    QCOMPARE(fn->type(), Function::ScriptType);

    Script *script = qobject_cast<Script*>(fn);
    QVERIFY(script != nullptr);
    QVERIFY(script->data().contains("startfunction:0"));
    QVERIFY(script->data().contains("wait:500"));
    QCOMPARE(fn->runOrder(), Function::Loop);
}

void AgentContext_Test::scriptSerialization()
{
    // Create a Script and verify it appears in workspace sync serialization
    Script *s = new Script(m_doc);
    s->setName("Serialize Me");
    s->setData("blackout:on\nwait:100\nblackout:off");
    m_doc->addFunction(s);
    quint32 fnId = s->id();

    AgentConnection conn(m_doc);
    QJsonObject fj = conn.serializeFunction(fnId);

    QCOMPARE(fj["type"].toString(), QString("Script"));
    QCOMPARE(fj["name"].toString(), QString("Serialize Me"));
    QVERIFY(fj.contains("data"));
    QCOMPARE(fj["data"].toString(), QString("blackout:on\nwait:100\nblackout:off"));
}

/*****************************************************************************
 * DMX reading and running functions
 *****************************************************************************/

void AgentContext_Test::getRunningFunctionsHandler()
{
    // With no functions running, should return empty list
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "get_running_functions";
    msg["requestId"] = "test-running-1";

    // Can't easily capture the WebSocket response in a unit test,
    // but we can verify the handler doesn't crash and the function
    // iteration logic works on an empty/populated Doc
    conn.handleGetRunningFunctions(msg);

    // Also verify with a function in the doc (not running)
    Scene *s = new Scene(m_doc);
    s->setName("Not Running");
    m_doc->addFunction(s);

    conn.handleGetRunningFunctions(msg);
    // No crash = success (actual response verification needs integration test)
}

/*****************************************************************************
 * modify_function: set_values vs add_values
 *****************************************************************************/

void AgentContext_Test::modifySceneSetValues()
{
    // Create a scene with 3 values
    Scene *scene = new Scene(m_doc);
    scene->setName("Test Scene");
    scene->setValue(0, 0, 100);  // fixture 0, ch 0
    scene->setValue(0, 1, 200);  // fixture 0, ch 1
    scene->setValue(0, 2, 50);   // fixture 0, ch 2
    m_doc->addFunction(scene);
    QCOMPARE(scene->values().count(), 3);

    // Simulate modify_function with setValues — should REPLACE all values
    AgentConnection conn(m_doc);
    QJsonObject msg;
    msg["type"] = "modify_function";
    msg["requestId"] = "test-set-1";
    msg["functionId"] = (int)scene->id();
    QJsonObject changes;
    QJsonArray setVals;
    QJsonObject v1; v1["fixtureId"] = 0; v1["channel"] = 5; v1["value"] = 128;
    setVals.append(v1);
    changes["setValues"] = setVals;
    msg["changes"] = changes;

    conn.handleModifyFunction(msg);

    // Should have exactly 1 value (the old 3 were cleared)
    QCOMPARE(scene->values().count(), 1);
    QCOMPARE((int)scene->value(0, 5), 128);
    // Old values should be gone
    QCOMPARE((int)scene->value(0, 0), 0);
    QCOMPARE((int)scene->value(0, 1), 0);
    QCOMPARE((int)scene->value(0, 2), 0);
}

void AgentContext_Test::modifySceneAddValues()
{
    // Create a scene with 2 values
    Scene *scene = new Scene(m_doc);
    scene->setName("Test Scene 2");
    scene->setValue(0, 0, 100);  // fixture 0, ch 0
    scene->setValue(0, 1, 200);  // fixture 0, ch 1
    m_doc->addFunction(scene);
    QCOMPARE(scene->values().count(), 2);

    // Simulate modify_function with addValues — should ADD without clearing
    AgentConnection conn(m_doc);
    QJsonObject msg;
    msg["type"] = "modify_function";
    msg["requestId"] = "test-add-1";
    msg["functionId"] = (int)scene->id();
    QJsonObject changes;
    QJsonArray addVals;
    QJsonObject v1; v1["fixtureId"] = 0; v1["channel"] = 5; v1["value"] = 128;
    addVals.append(v1);
    changes["addValues"] = addVals;
    msg["changes"] = changes;

    conn.handleModifyFunction(msg);

    // Should have 3 values (original 2 + 1 added)
    QCOMPARE(scene->values().count(), 3);
    QCOMPARE((int)scene->value(0, 0), 100);  // preserved
    QCOMPARE((int)scene->value(0, 1), 200);  // preserved
    QCOMPARE((int)scene->value(0, 5), 128);  // added
}

/*****************************************************************************
 * AgentConnection state and auth
 *****************************************************************************/

void AgentContext_Test::connectionStateEnum()
{
    // Verify enum values match what QML expects
    QCOMPARE(static_cast<int>(AgentConnection::Disconnected), 0);
    QCOMPARE(static_cast<int>(AgentConnection::Authenticating), 1);
    QCOMPARE(static_cast<int>(AgentConnection::Connecting), 2);
    QCOMPARE(static_cast<int>(AgentConnection::WaitingForSync), 3);
    QCOMPARE(static_cast<int>(AgentConnection::Connected), 4);

    // Verify initial state
    AgentConnection conn(m_doc);
    QCOMPARE(conn.state(), AgentConnection::Disconnected);
    QCOMPARE(conn.stateInt(), 0);
}

void AgentContext_Test::authStateEnum()
{
    // Verify enum values match what QML expects
    // QML uses these integers directly: Authenticated=3, not 2
    QCOMPARE(static_cast<int>(AgentAuthManager::Unknown), 0);
    QCOMPARE(static_cast<int>(AgentAuthManager::LoggedOut), 1);
    QCOMPARE(static_cast<int>(AgentAuthManager::Authenticating), 2);
    QCOMPARE(static_cast<int>(AgentAuthManager::Authenticated), 3);
}

void AgentContext_Test::handlePastedJWT()
{
    AgentConnection conn(m_doc);
    AgentAuthManager *auth = conn.authManager();

    // Verify initial state
    QCOMPARE(auth->authState(), AgentAuthManager::Unknown);
    QVERIFY(auth->accessToken().isEmpty());

    // Paste a fake JWT (starts with eyJ)
    QString fakeJWT = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ0ZXN0IjoxfQ.fake";

    QSignalSpy authSpy(auth, &AgentAuthManager::authenticated);
    QSignalSpy stateSpy(auth, &AgentAuthManager::authStateChanged);

    auth->handlePastedToken(fakeJWT);

    // Should transition to Authenticated and emit authenticated signal
    QCOMPARE(auth->authState(), AgentAuthManager::Authenticated);
    QCOMPARE(auth->accessToken(), fakeJWT);
    QCOMPARE(authSpy.count(), 1);
    QCOMPARE(authSpy.at(0).at(0).toString(), fakeJWT);

    // State change should have been emitted
    QVERIFY(stateSpy.count() >= 1);
    // Last state change should be Authenticated
    QCOMPARE(stateSpy.last().at(0).value<AgentAuthManager::AuthState>(),
             AgentAuthManager::Authenticated);
}

/*****************************************************************************
 * create_fixture handler
 *****************************************************************************/

void AgentContext_Test::createFixtureHandler()
{
    // Set up a fixture definition in the cache
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("TestMfr");
    def->setModel("TestModel");
    def->setType(QLCFixtureDef::Scanner);

    QLCChannel *ch1 = new QLCChannel();
    ch1->setName("Pan");
    ch1->setGroup(QLCChannel::Pan);
    def->addChannel(ch1);

    QLCChannel *ch2 = new QLCChannel();
    ch2->setName("Tilt");
    ch2->setGroup(QLCChannel::Tilt);
    def->addChannel(ch2);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("2ch");
    mode->insertChannel(ch1, 0);
    mode->insertChannel(ch2, 1);
    def->addMode(mode);

    m_doc->fixtureDefCache()->addFixtureDef(def);

    int countBefore = m_doc->fixtures().count();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "create_fixture";
    msg["requestId"] = "test-cf-1";
    msg["name"] = "Test Scanner";
    msg["manufacturer"] = "TestMfr";
    msg["model"] = "TestModel";
    msg["mode"] = "2ch";
    msg["universe"] = 0;
    msg["address"] = 101;  // 1-based DMX address

    conn.handleCreateFixture(msg);

    QCOMPARE(m_doc->fixtures().count(), countBefore + 1);

    // Find the newly created fixture
    Fixture *fxi = nullptr;
    foreach (Fixture *f, m_doc->fixtures())
    {
        if (f->name() == "Test Scanner")
        {
            fxi = f;
            break;
        }
    }
    QVERIFY(fxi != nullptr);
    QCOMPARE(fxi->universe(), (quint32)0);
    QCOMPARE(fxi->address(), (quint32)100);  // 0-based internal (101 - 1)
    QCOMPARE(fxi->channels(), (quint32)2);
}

void AgentContext_Test::createFixtureAddressOverlap()
{
    // First create a fixture at address 100
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("TestMfr", "TestModel");
    QVERIFY(def != nullptr);

    Fixture *existing = new Fixture(m_doc);
    existing->setName("Existing");
    existing->setFixtureDefinition(def, def->mode("2ch"));
    existing->setUniverse(0);
    existing->setAddress(200);
    m_doc->addFixture(existing);

    int countBefore = m_doc->fixtures().count();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "create_fixture";
    msg["requestId"] = "test-cf-overlap";
    msg["name"] = "Overlapping";
    msg["manufacturer"] = "TestMfr";
    msg["model"] = "TestModel";
    msg["mode"] = "2ch";
    msg["universe"] = 0;
    msg["address"] = 201; // 1-based DMX; overlaps with existing at internal 200-201

    conn.handleCreateFixture(msg);

    // Should NOT have added a fixture due to overlap
    QCOMPARE(m_doc->fixtures().count(), countBefore);
}

void AgentContext_Test::createFixtureDefNotFound()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "create_fixture";
    msg["requestId"] = "test-cf-notfound";
    msg["name"] = "Bad Fixture";
    msg["manufacturer"] = "NonexistentMfr";
    msg["model"] = "NonexistentModel";
    msg["mode"] = "Basic";
    msg["universe"] = 0;
    msg["address"] = 300;

    int countBefore = m_doc->fixtures().count();
    conn.handleCreateFixture(msg);

    // Should not have added
    QCOMPARE(m_doc->fixtures().count(), countBefore);
}

/*****************************************************************************
 * delete_fixture handler
 *****************************************************************************/

void AgentContext_Test::deleteFixtureHandler()
{
    // Create a fixture to delete
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("TestMfr", "TestModel");
    QVERIFY(def != nullptr);

    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("Delete Me Fixture");
    fxi->setFixtureDefinition(def, def->mode("2ch"));
    fxi->setUniverse(0);
    fxi->setAddress(400);
    m_doc->addFixture(fxi);
    quint32 fxiId = fxi->id();

    QVERIFY(m_doc->fixture(fxiId) != nullptr);

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_fixture";
    msg["requestId"] = "test-df-1";
    msg["fixtureId"] = (int)fxiId;

    conn.handleDeleteFixture(msg);

    QVERIFY(m_doc->fixture(fxiId) == nullptr);
}

void AgentContext_Test::deleteFixtureNotFound()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_fixture";
    msg["requestId"] = "test-df-2";
    msg["fixtureId"] = 9999;

    // Should not crash — sends error result
    conn.handleDeleteFixture(msg);
}

/*****************************************************************************
 * search_fixture_library handler
 *****************************************************************************/

void AgentContext_Test::searchFixtureLibrary()
{
    // The fixture def cache is loaded in initTestCase with ~1710 fixtures.
    // Search for "Intimidator" — should find Chauvet fixtures.
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "search_fixture_library";
    msg["requestId"] = "test-sfl-1";
    msg["query"] = "Intimidator";

    // We can't easily check the WebSocket output in a unit test (not connected),
    // but we can verify it doesn't crash and exercise the code path.
    conn.handleSearchFixtureLibrary(msg);

    // Also test with a very specific query to verify the cache is being searched
    QStringList manufacturers = m_doc->fixtureDefCache()->manufacturers();
    QVERIFY(manufacturers.count() > 0);

    // Verify the cache has Chauvet fixtures (loaded from resources/fixtures)
    QStringList chauvetModels = m_doc->fixtureDefCache()->models("Chauvet");
    QVERIFY(chauvetModels.count() > 0);

    // Verify a search for empty string doesn't crash
    QJsonObject msg2;
    msg2["type"] = "search_fixture_library";
    msg2["requestId"] = "test-sfl-2";
    msg2["query"] = "";
    conn.handleSearchFixtureLibrary(msg2);
}

void AgentContext_Test::destructorSafety()
{
    // Verify AgentConnection can be destroyed without crashing
    // even when Doc might be in an indeterminate state.
    // This tests the fix for the shutdown crash where onWsDisconnected
    // tried to disconnect from a destroyed Doc.
    {
        AgentConnection *conn = new AgentConnection(m_doc);
        // No connection established — destructor should be safe
        delete conn;
    }

    // Verify Doc is still valid after AgentConnection destruction
    QVERIFY(m_doc != nullptr);
    QCOMPARE(m_doc->fixtures().count() + 1, m_doc->fixtures().count() + 1); // just access it
}

void AgentContext_Test::createPaletteHandler()
{
    int countBefore = m_doc->palettes().count();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "create_palette";
    msg["requestId"] = "test-cp-1";
    msg["name"] = "Deep Blue";
    msg["paletteType"] = "Color";
    QJsonArray vals;
    vals.append(QString("#0000cc"));
    msg["values"] = vals;

    conn.handleCreatePalette(msg);

    QCOMPARE(m_doc->palettes().count(), countBefore + 1);

    // Find the newly created palette
    QLCPalette *pal = nullptr;
    for (QLCPalette *p : m_doc->palettes())
    {
        if (p->name() == "Deep Blue")
        {
            pal = p;
            break;
        }
    }
    QVERIFY(pal != nullptr);
    QCOMPARE(pal->type(), QLCPalette::Color);
    QCOMPARE(pal->values().count(), 1);
    QCOMPARE(pal->values().at(0).toString(), QString("#0000cc"));
}

void AgentContext_Test::modifyPaletteHandler()
{
    // First create a palette
    QLCPalette *pal = new QLCPalette(QLCPalette::PanTilt);
    pal->setName("Original");
    pal->setValue(QVariant(180), QVariant(90));
    m_doc->addPalette(pal);
    quint32 palId = pal->id();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "modify_palette";
    msg["requestId"] = "test-mp-1";
    msg["paletteId"] = (int)palId;
    msg["name"] = "Center Stage";
    QJsonArray vals;
    vals.append(128);
    vals.append(64);
    msg["values"] = vals;
    msg["fanningType"] = "Linear";
    msg["fanningLayout"] = "XCentered";

    conn.handleModifyPalette(msg);

    QLCPalette *modified = m_doc->palette(palId);
    QVERIFY(modified != nullptr);
    QCOMPARE(modified->name(), QString("Center Stage"));
    QCOMPARE(modified->values().count(), 2);
    QCOMPARE(modified->values().at(0).toInt(), 128);
    QCOMPARE(modified->values().at(1).toInt(), 64);
    QCOMPARE(modified->fanningType(), QLCPalette::Linear);
    QCOMPARE(modified->fanningLayout(), QLCPalette::XCentered);
}

void AgentContext_Test::deletePaletteHandler()
{
    // First create a palette
    QLCPalette *pal = new QLCPalette(QLCPalette::Dimmer);
    pal->setName("Half");
    pal->setValue(QVariant(128));
    m_doc->addPalette(pal);
    quint32 palId = pal->id();

    QVERIFY(m_doc->palette(palId) != nullptr);

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_palette";
    msg["requestId"] = "test-dp-1";
    msg["paletteId"] = (int)palId;

    conn.handleDeletePalette(msg);

    QVERIFY(m_doc->palette(palId) == nullptr);
}

void AgentContext_Test::deletePaletteNotFound()
{
    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "delete_palette";
    msg["requestId"] = "test-dp-notfound";
    msg["paletteId"] = 99999;

    // Should not crash — just sends error result
    conn.handleDeletePalette(msg);

    // Verify it didn't crash (test passes if we get here)
    QVERIFY(true);
}

void AgentContext_Test::fixtureDefPhysicalSerialization()
{
    // Create a fixture def with physical properties
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("TestMfg");
    def->setModel("PhysModel");
    def->setType(QLCFixtureDef::MovingHead);

    QLCPhysical phys;
    phys.setFocusPanMax(540);
    phys.setFocusTiltMax(270);
    phys.setFocusType("Head");
    phys.setLensDegreesMin(15.0);
    phys.setLensDegreesMax(30.0);
    def->setPhysical(phys);

    // Add a channel so the def is valid
    QLCChannel *ch = new QLCChannel();
    ch->setName("Pan");
    ch->setGroup(QLCChannel::Pan);
    def->addChannel(ch);

    // Add a mode referencing the channel
    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("Default");
    mode->insertChannel(ch, 0);
    def->addMode(mode);

    // Add def to cache and create a fixture using it
    m_doc->fixtureDefCache()->addFixtureDef(def);

    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("Phys Test");
    fxi->setFixtureDefinition(def, mode);
    fxi->setUniverse(0);
    fxi->setAddress(0);
    m_doc->addFixture(fxi);

    // Serialize and verify
    AgentConnection conn(m_doc);
    QJsonObject sync = conn.buildWorkspaceSync();

    QJsonArray defs = sync["fixtureDefs"].toArray();
    bool found = false;
    for (const QJsonValue &v : defs)
    {
        QJsonObject dj = v.toObject();
        if (dj["manufacturer"].toString() == "TestMfg" &&
            dj["model"].toString() == "PhysModel")
        {
            found = true;
            QVERIFY(dj.contains("physical"));
            QJsonObject pj = dj["physical"].toObject();
            QCOMPARE(pj["focusPanMax"].toInt(), 540);
            QCOMPARE(pj["focusTiltMax"].toInt(), 270);
            QCOMPARE(pj["focusType"].toString(), QString("Head"));
            QCOMPARE(pj["lensDegreesMin"].toDouble(), 15.0);
            QCOMPARE(pj["lensDegreesMax"].toDouble(), 30.0);
            break;
        }
    }
    QVERIFY2(found, "TestMfg/PhysModel fixture def not found in serialization");
}

/*****************************************************************************
 * structuredData on AgentContext
 *****************************************************************************/

void AgentContext_Test::structuredDataIsEmpty()
{
    AgentContext ctx;
    QVERIFY(ctx.isEmpty());

    // Only structuredData set — should not be empty
    QJsonObject sd;
    sd["key"] = "value";
    ctx.structuredData = sd;
    QVERIFY(!ctx.isEmpty());

    // Clear it — should be empty again
    ctx.structuredData = QJsonObject();
    QVERIFY(ctx.isEmpty());
}

void AgentContext_Test::saveAndLoadStructuredData()
{
    AgentContext ctx;
    QJsonObject sd;
    sd["pan_offset"] = 12.5;
    sd["tilt_inverted"] = true;
    ctx.structuredData = sd;

    // Save
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    // Verify XML contains StructuredData element
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QString xml = QString::fromUtf8(buffer.readAll());
    QVERIFY(xml.contains("StructuredData"));
    QVERIFY(xml.contains("pan_offset"));
    buffer.close();

    // Load
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement(); // Root
    reader.readNextStartElement(); // AgentContext

    AgentContext ctx2;
    QVERIFY(ctx2.loadXML(reader));
    QVERIFY(ctx2.userNote.isEmpty());
    QVERIFY(ctx2.agentNote.isEmpty());
    QVERIFY(!ctx2.structuredData.isEmpty());
    QCOMPARE(ctx2.structuredData["pan_offset"].toDouble(), 12.5);
    QCOMPARE(ctx2.structuredData["tilt_inverted"].toBool(), true);
}

void AgentContext_Test::saveAndLoadAllFields()
{
    AgentContext ctx;
    ctx.userNote = "User note";
    ctx.agentNote = "Agent note";
    QJsonObject sd;
    sd["calibration"] = QJsonObject({{"pan_offset", 5.0}});
    ctx.structuredData = sd;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartElement("Root");
    ctx.saveXML(&writer);
    writer.writeEndElement();
    writer.writeEndDocument();
    writer.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();
    reader.readNextStartElement();

    AgentContext ctx2;
    QVERIFY(ctx2.loadXML(reader));
    QCOMPARE(ctx2.userNote, QString("User note"));
    QCOMPARE(ctx2.agentNote, QString("Agent note"));
    QVERIFY(!ctx2.structuredData.isEmpty());
    QVERIFY(ctx2.structuredData.contains("calibration"));
}

void AgentContext_Test::serializeStructuredDataInSync()
{
    QJsonObject sd;
    sd["session"] = QJsonObject({{"phase", "discovery"}});
    m_doc->setStructuredData(sd);
    m_doc->setAgentNote("workspace note");

    AgentConnection conn(m_doc);
    QJsonObject sync = conn.buildWorkspaceSync();

    QVERIFY(sync.contains("agentContext"));
    QJsonObject wsCtx = sync["agentContext"].toObject();
    QVERIFY(wsCtx.contains("structuredData"));
    QJsonObject syncSd = wsCtx["structuredData"].toObject();
    QVERIFY(syncSd.contains("session"));
    QCOMPARE(syncSd["session"].toObject()["phase"].toString(), QString("discovery"));
}

void AgentContext_Test::updateAgentNoteWithStructuredData()
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("SD Test");
    fxi->setUniverse(0);
    fxi->setAddress(0);
    fxi->setChannels(4);
    m_doc->addFixture(fxi);
    quint32 fxiId = fxi->id();

    AgentConnection conn(m_doc);

    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-sd-1";
    msg["agentNote"] = "Some note";
    QJsonObject sd;
    sd["calibration"] = QJsonObject({{"pan_offset", 10.0}});
    msg["structuredData"] = sd;
    QJsonObject target;
    target["type"] = "fixture";
    target["id"] = (int)fxiId;
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    // Verify both agentNote and structuredData were set
    QCOMPARE(m_doc->fixture(fxiId)->agentContext().agentNote, QString("Some note"));
    QVERIFY(!m_doc->fixture(fxiId)->agentContext().structuredData.isEmpty());
    QCOMPARE(m_doc->fixture(fxiId)->agentContext().structuredData["calibration"].toObject()["pan_offset"].toDouble(), 10.0);
}

void AgentContext_Test::deltaIncludesStructuredData()
{
    // Test that update without structuredData does not set it
    Fixture *fxi = new Fixture(m_doc);
    fxi->setName("Delta Test");
    fxi->setUniverse(0);
    fxi->setAddress(0);
    fxi->setChannels(4);
    QJsonObject existingSd;
    existingSd["existing"] = "data";
    fxi->setStructuredData(existingSd);
    m_doc->addFixture(fxi);
    quint32 fxiId = fxi->id();

    AgentConnection conn(m_doc);

    // Send update WITHOUT structuredData — existing should be preserved
    QJsonObject msg;
    msg["type"] = "update_agent_note";
    msg["requestId"] = "test-delta-1";
    msg["agentNote"] = "New note";
    QJsonObject target;
    target["type"] = "fixture";
    target["id"] = (int)fxiId;
    msg["target"] = target;

    conn.handleUpdateAgentNote(msg);

    QCOMPARE(m_doc->fixture(fxiId)->agentContext().agentNote, QString("New note"));
    // structuredData should be preserved (not cleared)
    QVERIFY(!m_doc->fixture(fxiId)->agentContext().structuredData.isEmpty());
    QCOMPARE(m_doc->fixture(fxiId)->agentContext().structuredData["existing"].toString(), QString("data"));
}

#ifdef QMLUI
QTEST_MAIN(AgentContext_Test)
#else
QTEST_APPLESS_MAIN(AgentContext_Test)
#endif
