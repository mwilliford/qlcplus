/*
  Q Light Controller Plus
  LayoutPanel.qml

  Copyright (c) Marcus Williford

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt
*/

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle
{
    id: root
    color: "#2a2a2a"

    // Compact spinbox component — fixed 28px height, dark theme
    component CompactSpin: SpinBox
    {
        implicitHeight: 28
        font.pixelSize: 11

        background: Rectangle
        {
            color: "#333"
            radius: 3
            border.color: parent.activeFocus ? "#4a9eff" : "#555"
            border.width: 1
        }

        contentItem: TextInput
        {
            text: parent.textFromValue(parent.value, parent.locale)
            font.pixelSize: 11
            color: "#ccc"
            selectionColor: "#4a9eff"
            selectedTextColor: "#fff"
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            readOnly: !parent.editable
            validator: parent.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
        }

        up.indicator: Rectangle
        {
            x: parent.width - width; y: 0
            width: 20; height: parent.height / 2
            color: parent.up.pressed ? "#555" : "transparent"
            Text { text: "+"; font.pixelSize: 9; color: "#999"; anchors.centerIn: parent }
        }

        down.indicator: Rectangle
        {
            x: parent.width - width; y: parent.height / 2
            width: 20; height: parent.height / 2
            color: parent.down.pressed ? "#555" : "transparent"
            Text { text: "\u2212"; font.pixelSize: 9; color: "#999"; anchors.centerIn: parent }
        }
    }

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // --- Mode selector ---
        Row
        {
            Layout.fillWidth: true
            spacing: 2

            Repeater
            {
                model: ["Layout", "Calibrate", "Focus", "Live"]

                Button
                {
                    width: (root.width - 16 - 6) / 4
                    height: 28
                    text: modelData
                    checkable: true
                    checked: spatialController.mode === index
                    autoExclusive: true

                    onClicked: spatialController.mode = index

                    background: Rectangle
                    {
                        color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                        radius: 3
                    }

                    contentItem: Text
                    {
                        text: parent.text
                        color: parent.checked ? "#fff" : "#aaa"
                        font.pixelSize: 11
                        font.bold: parent.checked
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ===============================================================
        // LAYOUT MODE (mode === 0)
        // ===============================================================
        ColumnLayout
        {
            visible: spatialController.mode === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

        // --- Gizmo mode (W/E shortcuts) ---
        RowLayout
        {
            spacing: 4

            Text { text: "Tool:"; color: "#999"; font.pixelSize: 11;
                   Layout.alignment: Qt.AlignVCenter }

            Button
            {
                text: "Move (W)"
                checkable: true
                checked: spatialController.gizmoMode === 0
                autoExclusive: true
                implicitHeight: 26; Layout.fillWidth: true

                onClicked: spatialController.gizmoMode = 0

                background: Rectangle
                {
                    color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                    radius: 3
                }
                contentItem: Text
                {
                    text: parent.text; color: parent.checked ? "#fff" : "#aaa"
                    font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button
            {
                text: "Rotate (E)"
                checkable: true
                checked: spatialController.gizmoMode === 1
                autoExclusive: true
                implicitHeight: 26; Layout.fillWidth: true

                onClicked: spatialController.gizmoMode = 1

                background: Rectangle
                {
                    color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                    radius: 3
                }
                contentItem: Text
                {
                    text: parent.text; color: parent.checked ? "#fff" : "#aaa"
                    font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // --- Separator ---
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        // --- Selection header ---
        Text
        {
            text: {
                if (!spatialController.hasSelection)
                    return "No Selection"
                if (spatialController.selectionCount > 1)
                    return spatialController.selectionCount + " fixtures selected"
                return spatialController.selectedFixtureName
            }
            color: spatialController.hasSelection ? "#ccc" : "#666"
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        // --- Align tools (visible with multi-select) ---
        RowLayout
        {
            visible: spatialController.selectionCount > 1
            Layout.fillWidth: true
            spacing: 4

            Text { text: "Align:"; color: "#999"; font.pixelSize: 11;
                   Layout.alignment: Qt.AlignVCenter }

            Button
            {
                text: "X"; implicitWidth: 36; implicitHeight: 26
                onClicked: spatialController.alignSelection("X")
                background: Rectangle { color: parent.hovered ? "#e74c3c" : "#444"; radius: 3 }
                contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11;
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button
            {
                text: "Y"; implicitWidth: 36; implicitHeight: 26
                onClicked: spatialController.alignSelection("Y")
                background: Rectangle { color: parent.hovered ? "#2ecc71" : "#444"; radius: 3 }
                contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11;
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            Button
            {
                text: "Z"; implicitWidth: 36; implicitHeight: 26
                onClicked: spatialController.alignSelection("Z")
                background: Rectangle { color: parent.hovered ? "#3498db" : "#444"; radius: 3 }
                contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11;
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
        }

        // --- Properties (visible only when a single fixture is selected) ---
        ColumnLayout
        {
            visible: spatialController.hasSelection && spatialController.selectionCount === 1
            Layout.fillWidth: true
            spacing: 4

            // --- Position ---
            Text { text: "Position (m)"; color: "#999"; font.pixelSize: 11 }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 4

                Text { text: "X"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: posXSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posX * 100)
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.posX = value / 100.0 }
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            posXSpin.updating = true
                            posXSpin.value = Math.round(spatialController.posX * 100)
                            posXSpin.updating = false
                        }
                    }
                }

                Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: posYSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posY * 100)
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.posY = value / 100.0 }
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            posYSpin.updating = true
                            posYSpin.value = Math.round(spatialController.posY * 100)
                            posYSpin.updating = false
                        }
                    }
                }

                Text { text: "Z"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: posZSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posZ * 100)
                    stepSize: 1
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.posZ = value / 100.0 }
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            posZSpin.updating = true
                            posZSpin.value = Math.round(spatialController.posZ * 100)
                            posZSpin.updating = false
                        }
                    }
                }
            }

            // --- Rotation ---
            Text { text: "Rotation (\u00B0)"; color: "#999"; font.pixelSize: 11 }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 4

                Text { text: "P"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: rotPSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotPitch * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.rotPitch = value / 10.0 }
                    textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            rotPSpin.updating = true
                            rotPSpin.value = Math.round(spatialController.rotPitch * 10)
                            rotPSpin.updating = false
                        }
                    }
                }

                Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: rotYSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotYaw * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.rotYaw = value / 10.0 }
                    textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            rotYSpin.updating = true
                            rotYSpin.value = Math.round(spatialController.rotYaw * 10)
                            rotYSpin.updating = false
                        }
                    }
                }

                Text { text: "R"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                CompactSpin
                {
                    id: rotRSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotRoll * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true
                    property bool updating: false
                    onValueModified: { if (!updating) spatialController.rotRoll = value / 10.0 }
                    textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                    Connections {
                        target: spatialController
                        function onTransformChanged() {
                            rotRSpin.updating = true
                            rotRSpin.value = Math.round(spatialController.rotRoll * 10)
                            rotRSpin.updating = false
                        }
                    }
                }
            }
        }

        // --- Separator ---
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        // --- Snap settings ---
        Text { text: "Snap"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

        RowLayout
        {
            Layout.fillWidth: true
            spacing: 6

            Button
            {
                text: "Grid"
                checkable: true
                checked: spatialController.gridSnap
                implicitWidth: 50; implicitHeight: 28

                onClicked: spatialController.gridSnap = checked

                background: Rectangle
                {
                    color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                    radius: 3
                }
                contentItem: Text
                {
                    text: parent.text; color: parent.checked ? "#fff" : "#aaa"
                    font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            CompactSpin
            {
                id: gridSizeSpin
                from: 1; to: 500
                value: Math.round(spatialController.gridSize * 100)
                stepSize: 5
                Layout.fillWidth: true
                editable: true
                enabled: spatialController.gridSnap

                onValueModified: spatialController.gridSize = value / 100.0

                textFromValue: function(v) { return (v / 100.0).toFixed(2) + " m" }
                valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }

                Connections
                {
                    target: spatialController
                    function onGridSizeChanged()
                    {
                        gridSizeSpin.value = Math.round(spatialController.gridSize * 100)
                    }
                }
            }
        }

        // --- Axis mode ---
        RowLayout
        {
            spacing: 6

            Text { text: "Axes:"; color: "#999"; font.pixelSize: 11;
                   Layout.alignment: Qt.AlignVCenter }

            Button
            {
                text: "World"
                checkable: true
                checked: spatialController.axisMode === 0
                autoExclusive: true
                implicitWidth: 55; implicitHeight: 28

                onClicked: spatialController.axisMode = 0

                background: Rectangle
                {
                    color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                    radius: 3
                }
                contentItem: Text
                {
                    text: parent.text; color: parent.checked ? "#fff" : "#aaa"
                    font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button
            {
                text: "Local"
                checkable: true
                checked: spatialController.axisMode === 1
                autoExclusive: true
                implicitWidth: 55; implicitHeight: 28

                onClicked: spatialController.axisMode = 1

                background: Rectangle
                {
                    color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333")
                    radius: 3
                }
                contentItem: Text
                {
                    text: parent.text; color: parent.checked ? "#fff" : "#aaa"
                    font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // --- Camera presets ---
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        Text { text: "Camera"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

        Row
        {
            spacing: 4

            Repeater
            {
                model: ["FOH", "Top", "Front", "Side"]

                Button
                {
                    width: 56; height: 28
                    text: modelData

                    onClicked: spatialController.setCameraPreset(modelData)

                    background: Rectangle
                    {
                        color: parent.hovered ? "#444" : "#333"
                        radius: 3
                    }
                    contentItem: Text
                    {
                        text: parent.text; color: "#aaa"
                        font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // --- Truss ---
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        Text { text: "Truss"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

        Button
        {
            text: "+ Add Truss"
            implicitHeight: 28
            Layout.fillWidth: true

            onClicked: spatialController.addDefaultTruss()

            background: Rectangle
            {
                color: parent.hovered ? "#444" : "#333"
                radius: 3
            }
            contentItem: Text
            {
                text: parent.text; color: "#aaa"
                font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        } // end Layout mode ColumnLayout

        // ===============================================================
        // CALIBRATE MODE (mode === 1)
        // ===============================================================
        ColumnLayout
        {
            id: calibratePanel
            visible: spatialController.mode === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            // Observation data (refreshed on calibrationChanged)
            property var obsModel: []
            property var solverResults: []

            Connections
            {
                target: calibrateController
                function onChanged()
                {
                    calibratePanel.obsModel = calibrateController.observationsList()
                    calibratePanel.solverResults = calibrateController.solverFixtureResults()
                }
            }

            // --- Observation list ---
            RowLayout
            {
                Layout.fillWidth: true

                Text
                {
                    text: "Observations (" + calibrateController.obsCount + ")"
                    color: "#ccc"; font.pixelSize: 13; font.bold: true
                    Layout.fillWidth: true
                }

                Button
                {
                    text: "Clear"
                    implicitWidth: 45; implicitHeight: 24
                    visible: calibrateController.obsCount > 0
                    onClicked: calibrateController.clearAllObs()
                    background: Rectangle { color: parent.hovered ? "#644" : "#433"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#c99"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            ListView
            {
                id: obsListView
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 200)
                clip: true
                spacing: 2
                model: calibratePanel.obsModel

                delegate: Rectangle
                {
                    width: obsListView.width
                    height: 36
                    color: index % 2 ? "#383838" : "#333"
                    radius: 3

                    RowLayout
                    {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 4

                        Text
                        {
                            text: modelData.description || ""
                            color: "#ccc"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Button
                        {
                            text: "\u00d7"
                            implicitWidth: 22; implicitHeight: 22
                            onClicked: calibrateController.removeObs(modelData.id)
                            background: Rectangle { color: parent.hovered ? "#a33" : "transparent"; radius: 3 }
                            contentItem: Text { text: parent.text; color: "#c66"; font.pixelSize: 13;
                                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Text
                {
                    visible: parent.count === 0
                    anchors.centerIn: parent
                    text: "No observations yet"
                    color: "#666"; font.pixelSize: 11
                }
            }

            // --- Separator ---
            Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

            // --- Add observation ---
            Text { text: "Add Observation"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

            // Height observation (Position Z)
            RowLayout
            {
                Layout.fillWidth: true
                spacing: 4
                enabled: spatialController.hasSelection

                Text { text: "Height"; color: "#999"; font.pixelSize: 11; Layout.preferredWidth: 46 }

                CompactSpin
                {
                    id: heightSpin
                    from: 0; to: 2000
                    value: 300  // default 3.00m
                    stepSize: 5
                    Layout.fillWidth: true
                    editable: true
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) + "m" }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                }

                Button
                {
                    text: "+ Add"
                    implicitWidth: 50; implicitHeight: 28
                    enabled: spatialController.hasSelection
                    onClicked: calibrateController.addPositionObs(
                        spatialController.selectedFixtureId, 2,
                        heightSpin.value / 100.0, 0.95)
                    background: Rectangle { color: parent.enabled ? (parent.hovered ? "#4a9eff" : "#3a7fcc") : "#444"; radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#fff" : "#666"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            // Distance observation
            RowLayout
            {
                Layout.fillWidth: true
                spacing: 4

                Text { text: "Dist"; color: "#999"; font.pixelSize: 11; Layout.preferredWidth: 30 }

                CompactSpin
                {
                    id: distFixA
                    from: 0; to: 999
                    value: 0
                    stepSize: 1
                    implicitWidth: 36
                    editable: true
                    textFromValue: function(v) { return v.toString() }
                    valueFromText: function(t) { return parseInt(t) || 0 }
                }

                Text { text: "\u2194"; color: "#999"; font.pixelSize: 11 }

                CompactSpin
                {
                    id: distFixB
                    from: 0; to: 999
                    value: 1
                    stepSize: 1
                    implicitWidth: 36
                    editable: true
                    textFromValue: function(v) { return v.toString() }
                    valueFromText: function(t) { return parseInt(t) || 0 }
                }

                CompactSpin
                {
                    id: distValueSpin
                    from: 1; to: 5000
                    value: 300  // 3.00m
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) + "m" }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                }

                Button
                {
                    text: "+"
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: calibrateController.addDistanceObs(
                        distFixA.value, distFixB.value,
                        distValueSpin.value / 100.0, 0.90)
                    background: Rectangle { color: parent.hovered ? "#4a9eff" : "#3a7fcc"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            // --- Separator ---
            Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

            // --- Solver status ---
            Text { text: "Solver"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

            ColumnLayout
            {
                visible: calibrateController.hasSolverResult
                Layout.fillWidth: true
                spacing: 2

                Text
                {
                    text: (calibrateController.solverConverged ? "\u2714 Converged" : "\u2718 Not converged")
                          + " (RMS " + calibrateController.solverRms.toFixed(3) + "m)"
                    color: calibrateController.solverConverged ? "#2ecc71" : "#e74c3c"
                    font.pixelSize: 11
                }

                Repeater
                {
                    model: calibratePanel.solverResults

                    Text
                    {
                        text: modelData.fixtureName + ": \u00b1"
                              + Math.max(modelData.xCm, modelData.yCm, modelData.zCm).toFixed(0) + "cm"
                        color: modelData.quality === "good" ? "#2ecc71" :
                               modelData.quality === "moderate" ? "#f39c12" : "#e74c3c"
                        font.pixelSize: 11
                    }
                }
            }

            Text
            {
                visible: !calibrateController.hasSolverResult
                text: calibrateController.obsCount > 0 ? "Ready to solve" : "Add observations first"
                color: "#666"; font.pixelSize: 11
            }

            // --- Action buttons ---
            RowLayout
            {
                Layout.fillWidth: true
                spacing: 4

                Button
                {
                    text: "Solve"
                    implicitHeight: 30
                    Layout.fillWidth: true
                    enabled: calibrateController.obsCount > 0
                    onClicked: calibrateController.runSolve()
                    background: Rectangle { color: parent.enabled ? (parent.hovered ? "#4a9eff" : "#3a7fcc") : "#444"; radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#fff" : "#666"; font.pixelSize: 12; font.bold: true;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                Button
                {
                    text: "Accept"
                    implicitHeight: 30
                    Layout.fillWidth: true
                    visible: calibrateController.hasSolverResult && calibrateController.solverConverged
                    onClicked: calibrateController.acceptSolverResults()
                    background: Rectangle { color: parent.hovered ? "#2ecc71" : "#27ae60"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 12; font.bold: true;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                Button
                {
                    text: "Dismiss"
                    implicitHeight: 30
                    visible: calibrateController.hasSolverResult
                    onClicked: calibrateController.dismissSolverResults()
                    background: Rectangle { color: parent.hovered ? "#644" : "#433"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#c99"; font.pixelSize: 11;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        } // end Calibrate mode ColumnLayout

        // --- Spacer ---
        Item { Layout.fillHeight: true }

        // --- Status ---
        Text
        {
            text: "Spatial View"
            color: "#666"
            font.pixelSize: 10
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
