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

        // ===============================================================
        // ALWAYS-VISIBLE TOP: Mode selector
        // ===============================================================
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
        ScrollView
        {
            id: layoutScroll
            visible: spatialController.mode === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout
        {
            id: layoutPanel
            width: layoutScroll.availableWidth
            spacing: 6

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

            // --- Tool + Frame combined row (visible only with selection) ---
            // Two independent button groups so toggling one doesn't affect the other
            ButtonGroup { id: toolGroup }
            ButtonGroup { id: frameGroup }

            RowLayout
            {
                visible: spatialController.hasSelection
                Layout.fillWidth: true
                spacing: 4

                // Tool: Move / Rotate
                Button
                {
                    text: "\u2194"  // ↔ horizontal arrows = move
                    checkable: true
                    checked: spatialController.gizmoMode === 0
                    ButtonGroup.group: toolGroup
                    implicitWidth: 26; implicitHeight: 22
                    onClicked: spatialController.gizmoMode = 0
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Move (W) — translate gizmo"
                    background: Rectangle { color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333"); radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.checked ? "#fff" : "#aaa";
                        font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                Button
                {
                    text: "\u21BB"  // ↻ clockwise arrow = rotate
                    checkable: true
                    checked: spatialController.gizmoMode === 1
                    ButtonGroup.group: toolGroup
                    implicitWidth: 26; implicitHeight: 22
                    onClicked: spatialController.gizmoMode = 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Rotate (E) — rotate gizmo"
                    background: Rectangle { color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333"); radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.checked ? "#fff" : "#aaa";
                        font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                // Separator
                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#444" }

                // Frame: World / Local
                Button
                {
                    text: "W"
                    checkable: true
                    checked: spatialController.axisMode === 0
                    ButtonGroup.group: frameGroup
                    implicitWidth: 26; implicitHeight: 22
                    onClicked: spatialController.axisMode = 0
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "World frame — gizmo aligns to world axes"
                    background: Rectangle { color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333"); radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.checked ? "#fff" : "#aaa";
                        font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                Button
                {
                    text: "L"
                    checkable: true
                    checked: spatialController.axisMode === 1
                    ButtonGroup.group: frameGroup
                    implicitWidth: 26; implicitHeight: 22
                    onClicked: spatialController.axisMode = 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: "Local frame — gizmo aligns to fixture orientation"
                    background: Rectangle { color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333"); radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.checked ? "#fff" : "#aaa";
                        font.pixelSize: 11; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }

                Item { Layout.fillWidth: true }  // spacer
            }

            // ===== NO SELECTION: Workspace tools =====
            ColumnLayout
            {
                visible: !spatialController.hasSelection
                Layout.fillWidth: true
                spacing: 6

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                Text { text: "Workspace"; color: "#ccc"; font.pixelSize: 13; font.bold: true }

                Button
                {
                    text: "+ Add Truss"
                    implicitHeight: 24
                    Layout.fillWidth: true
                    onClicked: spatialController.addDefaultTruss()
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: "Add a default truss (3m pipe at Z=3m)"
                    background: Rectangle { color: parent.hovered ? "#444" : "#333"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 11;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            // ===== MULTI-SELECT: Align tools =====
            ColumnLayout
            {
                visible: spatialController.selectionCount > 1
                Layout.fillWidth: true
                spacing: 4

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    Text { text: "Align:"; color: "#999"; font.pixelSize: 11;
                           Layout.alignment: Qt.AlignVCenter }

                    Button
                    {
                        text: "X"; implicitWidth: 30; implicitHeight: 22
                        onClicked: spatialController.alignSelection("X")
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Align selected fixtures on X axis"
                        background: Rectangle { color: parent.hovered ? "#e74c3c" : "#444"; radius: 3 }
                        contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11; font.bold: true;
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button
                    {
                        text: "Y"; implicitWidth: 30; implicitHeight: 22
                        onClicked: spatialController.alignSelection("Y")
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Align selected fixtures on Y axis"
                        background: Rectangle { color: parent.hovered ? "#2ecc71" : "#444"; radius: 3 }
                        contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11; font.bold: true;
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    Button
                    {
                        text: "Z"; implicitWidth: 30; implicitHeight: 22
                        onClicked: spatialController.alignSelection("Z")
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Align selected fixtures on Z axis"
                        background: Rectangle { color: parent.hovered ? "#3498db" : "#444"; radius: 3 }
                        contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11; font.bold: true;
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }

                    Item { Layout.fillWidth: true }  // spacer
                }
            }

            // ===== SINGLE-SELECT: Position + Rotation properties =====
            ColumnLayout
            {
                visible: spatialController.hasSelection && spatialController.selectionCount === 1
                Layout.fillWidth: true
                spacing: 4

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

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

            // ===== SELECTION (1 OR MORE): Tolerance (solver prior) =====
            ColumnLayout
            {
                id: tolerancePanel
                visible: spatialController.hasSelection
                Layout.fillWidth: true
                spacing: 4

                // Refresh tolerance spinbox values when selection or solver state changes
                property int primaryId: spatialController.selectedFixtureId
                property bool updatingTols: false

                function reloadTolerances() {
                    if (primaryId < 0)
                        return
                    updatingTols = true
                    tolPosX.value = Math.round(calibrateController.getTolerance(primaryId, 0) * 100)
                    tolPosY.value = Math.round(calibrateController.getTolerance(primaryId, 1) * 100)
                    tolPosZ.value = Math.round(calibrateController.getTolerance(primaryId, 2) * 100)
                    tolRotP.value = Math.round(calibrateController.getTolerance(primaryId, 3) * 10)
                    tolRotY.value = Math.round(calibrateController.getTolerance(primaryId, 4) * 10)
                    tolRotR.value = Math.round(calibrateController.getTolerance(primaryId, 5) * 10)
                    updatingTols = false
                }

                onPrimaryIdChanged: reloadTolerances()
                Component.onCompleted: reloadTolerances()

                Connections {
                    target: calibrateController
                    function onChanged() { tolerancePanel.reloadTolerances() }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

                Text {
                    text: spatialController.selectionCount > 1
                          ? "Tolerance (applies to all)"
                          : "Tolerance (solver prior)"
                    color: "#ccc"
                    font.pixelSize: 13
                    font.bold: true
                }

                Text { text: "Pos (m)"; color: "#999"; font.pixelSize: 10 }

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    Text { text: "X"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolPosX
                        from: 0; to: 10000
                        value: 50  // 0.50m default
                        stepSize: 5
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 0, value / 100.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 0, value / 100.0)
                            }
                        }
                    }

                    Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolPosY
                        from: 0; to: 10000
                        value: 50
                        stepSize: 5
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 1, value / 100.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 1, value / 100.0)
                            }
                        }
                    }

                    Text { text: "Z"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolPosZ
                        from: 0; to: 10000
                        value: 50
                        stepSize: 5
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 2, value / 100.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 2, value / 100.0)
                            }
                        }
                    }
                }

                Text { text: "Rot (\u00B0)"; color: "#999"; font.pixelSize: 10 }

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    Text { text: "P"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolRotP
                        from: 0; to: 36000
                        value: 150  // 15.0°
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 3, value / 10.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 3, value / 10.0)
                            }
                        }
                    }

                    Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolRotY
                        from: 0; to: 36000
                        value: 150
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 4, value / 10.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 4, value / 10.0)
                            }
                        }
                    }

                    Text { text: "R"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 12 }
                    CompactSpin
                    {
                        id: tolRotR
                        from: 0; to: 36000
                        value: 150
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        textFromValue: function(v) { return (v / 10.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 10) }
                        onValueModified: {
                            if (tolerancePanel.updatingTols) return
                            if (spatialController.selectionCount > 1) {
                                calibrateController.setToleranceForFixtures(
                                    spatialController.selectedFixtureIds(), 5, value / 10.0)
                            } else if (tolerancePanel.primaryId >= 0) {
                                calibrateController.setTolerance(tolerancePanel.primaryId, 5, value / 10.0)
                            }
                        }
                    }
                }

                Button
                {
                    text: "Reset to defaults"
                    implicitHeight: 24
                    Layout.fillWidth: true
                    onClicked: {
                        if (spatialController.selectionCount > 1)
                            calibrateController.resetTolerancesForSelection(spatialController.selectedFixtureIds())
                        else if (tolerancePanel.primaryId >= 0)
                            calibrateController.resetTolerances(tolerancePanel.primaryId)
                    }
                    background: Rectangle { color: parent.hovered ? "#444" : "#333"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

        } // end Layout mode ColumnLayout
        } // end ScrollView wrapping Layout

        // ===============================================================
        // CALIBRATE MODE (mode === 1)
        // ===============================================================
        ScrollView
        {
            id: calibrateScroll
            visible: spatialController.mode === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout
        {
            id: calibratePanel
            width: calibrateScroll.availableWidth
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
            GridLayout
            {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: 2
                columnSpacing: 4
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
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: "Measured height in meters"
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) + "m" }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                }

                CompactSpin
                {
                    id: heightSigmaSpin
                    from: 1; to: 500
                    value: 15  // default ±15cm
                    stepSize: 5
                    implicitWidth: 68
                    editable: true
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: "Measurement uncertainty (±σ)"
                    textFromValue: function(v) { return "±" + v + "cm" }
                    valueFromText: function(t) { return Math.round(parseFloat(t.replace("±","").replace("cm",""))) }
                }

                Button
                {
                    text: "+ Add"
                    implicitWidth: 50; implicitHeight: 28
                    enabled: spatialController.hasSelection
                    onClicked: calibrateController.addPositionObs(
                        spatialController.selectedFixtureId, 2,
                        heightSpin.value / 100.0,
                        heightSigmaSpin.value / 100.0)
                    background: Rectangle { color: parent.enabled ? (parent.hovered ? "#4a9eff" : "#3a7fcc") : "#444"; radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#fff" : "#666"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            // Distance observation (uses current multi-selection)
            GridLayout
            {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: 2
                columnSpacing: 4
                enabled: spatialController.selectionCount === 2

                Text {
                    text: spatialController.selectionCount === 2
                          ? "Dist A\u2194B"
                          : "Dist (sel 2)"
                    color: spatialController.selectionCount === 2 ? "#999" : "#666"
                    font.pixelSize: 11
                    Layout.preferredWidth: 72
                }

                CompactSpin
                {
                    id: distValueSpin
                    from: 1; to: 5000
                    value: 300  // 3.00m
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: "Measured distance between fixtures (meters)"
                    textFromValue: function(v) { return (v / 100.0).toFixed(2) + "m" }
                    valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                }

                CompactSpin
                {
                    id: distSigmaSpin
                    from: 1; to: 500
                    value: 20  // default ±20cm
                    stepSize: 5
                    implicitWidth: 68
                    editable: true
                    ToolTip.visible: hovered; ToolTip.delay: 500
                    ToolTip.text: "Measurement uncertainty (±σ)"
                    textFromValue: function(v) { return "±" + v + "cm" }
                    valueFromText: function(t) { return Math.round(parseFloat(t.replace("±","").replace("cm",""))) }
                }

                Button
                {
                    text: "+ Add"
                    implicitWidth: 50; implicitHeight: 28
                    enabled: spatialController.selectionCount === 2
                    onClicked: {
                        var ids = spatialController.selectedFixtureIds()
                        if (ids.length >= 2) {
                            calibrateController.addDistanceObs(
                                ids[0], ids[1],
                                distValueSpin.value / 100.0,
                                distSigmaSpin.value / 100.0)
                        }
                    }
                    background: Rectangle { color: parent.enabled ? (parent.hovered ? "#4a9eff" : "#3a7fcc") : "#444"; radius: 3 }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#fff" : "#666"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }

            // Aim observation — beam aimed at a known point
            // Two rows: XYZ target spinners, then ± sigma + Add button
            ColumnLayout
            {
                Layout.fillWidth: true
                spacing: 2
                enabled: spatialController.hasSelection && spatialController.selectionCount === 1

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Aim \u2192"
                        color: (spatialController.hasSelection && spatialController.selectionCount === 1) ? "#999" : "#666"
                        font.pixelSize: 11
                        Layout.preferredWidth: 46
                    }

                    CompactSpin
                    {
                        id: aimX
                        from: -10000; to: 10000
                        value: 0
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Target X (m)"
                        textFromValue: function(v) { return (v / 100.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    }

                    CompactSpin
                    {
                        id: aimY
                        from: -10000; to: 10000
                        value: 0
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Target Y (m)"
                        textFromValue: function(v) { return (v / 100.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    }

                    CompactSpin
                    {
                        id: aimZ
                        from: -10000; to: 10000
                        value: 0
                        stepSize: 10
                        Layout.fillWidth: true
                        editable: true
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Target Z (m)"
                        textFromValue: function(v) { return (v / 100.0).toFixed(1) }
                        valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                    }
                }

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 4

                    Item { Layout.preferredWidth: 46 }  // label column spacer

                    CompactSpin
                    {
                        id: aimSigmaSpin
                        from: 1; to: 1000
                        value: 20  // default ±20cm
                        stepSize: 5
                        Layout.fillWidth: true
                        editable: true
                        ToolTip.visible: hovered; ToolTip.delay: 500
                        ToolTip.text: "Aim uncertainty (±σ)"
                        textFromValue: function(v) { return "±" + v + "cm" }
                        valueFromText: function(t) { return Math.round(parseFloat(t.replace("±","").replace("cm",""))) }
                    }

                    Button
                    {
                        text: "+ Add"
                        implicitWidth: 50; implicitHeight: 28
                        enabled: spatialController.hasSelection && spatialController.selectionCount === 1
                        onClicked: calibrateController.addAimObs(
                            spatialController.selectedFixtureId,
                            aimX.value / 100.0,
                            aimY.value / 100.0,
                            aimZ.value / 100.0,
                            aimSigmaSpin.value / 100.0)
                        background: Rectangle { color: parent.enabled ? (parent.hovered ? "#4a9eff" : "#3a7fcc") : "#444"; radius: 3 }
                        contentItem: Text { text: parent.text; color: parent.enabled ? "#fff" : "#666"; font.pixelSize: 10;
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
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

                ListView
                {
                    id: solverResultsList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 180)
                    clip: true
                    spacing: 2
                    model: calibratePanel.solverResults
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle
                    {
                        width: solverResultsList.width
                        height: 32
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
                                text: modelData.fixtureName
                                color: "#ccc"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text
                            {
                                text: "\u00b1" + Math.max(modelData.xCm, modelData.yCm, modelData.zCm).toFixed(0) + "cm"
                                color: modelData.quality === "good" ? "#2ecc71" :
                                       modelData.quality === "moderate" ? "#f39c12" : "#e74c3c"
                                font.pixelSize: 10
                                Layout.preferredWidth: 52
                                horizontalAlignment: Text.AlignRight
                            }

                            Button
                            {
                                text: "\u2714"
                                implicitWidth: 22; implicitHeight: 22
                                visible: calibrateController.solverConverged
                                onClicked: calibrateController.acceptFixtureResult(parseInt(modelData.fixtureId))
                                background: Rectangle { color: parent.hovered ? "#2ecc71" : "#27ae60"; radius: 3 }
                                contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11;
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }

                            Button
                            {
                                text: "\u00d7"
                                implicitWidth: 22; implicitHeight: 22
                                onClicked: calibrateController.dismissFixtureResult(parseInt(modelData.fixtureId))
                                background: Rectangle { color: parent.hovered ? "#a33" : "transparent"; radius: 3 }
                                contentItem: Text { text: parent.text; color: "#c66"; font.pixelSize: 13;
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }
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
        } // end ScrollView wrapping Calibrate

        // ===============================================================
        // FOCUS MODE (mode === 2)
        // ===============================================================
        ScrollView
        {
            id: focusScroll
            visible: spatialController.mode === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

        ColumnLayout
        {
            id: focusPanel
            width: focusScroll.availableWidth
            spacing: 6

            // --- Header row ---
            RowLayout
            {
                Layout.fillWidth: true
                Text { text: "Focus Points"; color: "#ccc"; font.pixelSize: 13; font.bold: true
                       Layout.fillWidth: true }
                Text {
                    text: spatialController.focusPoints.length
                    color: "#888"; font.pixelSize: 11
                }
            }

            // --- 3D-view gesture hints ---
            Text {
                Layout.fillWidth: true
                text: "Hold F over 3D view to aim beam at cursor.\nH to highlight selected fixtures (open shutter + full dim).\nShift+click on floor to create a point."
                color: "#777"; font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            // --- Highlight status chip ---
            Rectangle {
                visible: spatialController.highlight
                Layout.fillWidth: true
                implicitHeight: 22
                radius: 11
                color: "#d4a017"
                Text {
                    anchors.centerIn: parent
                    text: "☀ Highlight ON — press H to release"
                    color: "#222"; font.pixelSize: 10; font.bold: true
                }
            }

            // --- Add button ---
            // Shift+click in the 3D view also creates a point at the clicked
            // position; this button drops one at stage center, 1.5m up.
            Button
            {
                text: "+ Add Focus Point"
                implicitHeight: 28
                Layout.fillWidth: true
                onClicked: {
                    var newId = spatialController.createFocusPoint(0, 0, 1.5)
                    spatialController.setSelectedFocusPointId(newId)
                }
                ToolTip.visible: hovered; ToolTip.delay: 500
                ToolTip.text: "Drop a focus point at stage center (or Shift+click in 3D view)"
                background: Rectangle { color: parent.hovered ? "#4a9eff" : "#3a7fcc"; radius: 3 }
                contentItem: Text { text: parent.text; color: "#fff"; font.pixelSize: 11; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }

            // --- Assign selected fixture(s) ---
            Button
            {
                text: {
                    var n = spatialController.selectionCount
                    if (n === 0) return "Assign fixtures (select some first)"
                    if (n === 1) return "Assign selected fixture"
                    return "Assign " + n + " fixtures"
                }
                implicitHeight: 26
                Layout.fillWidth: true
                enabled: spatialController.hasSelection && spatialController.selectedFocusPointId !== ""
                onClicked: {
                    var fpId = spatialController.selectedFocusPointId
                    if (fpId === "") return
                    var ids = spatialController.selectedFixtureIds()
                    for (var i = 0; i < ids.length; ++i)
                        spatialController.assignFixtureToFocusPoint(fpId, ids[i])
                }
                ToolTip.visible: hovered; ToolTip.delay: 500
                ToolTip.text: "Assign currently selected fixtures to the selected focus point"
                background: Rectangle {
                    color: !parent.enabled ? "#2a2a2a"
                           : parent.hovered ? "#4a9eff" : "#3a7fcc"
                    radius: 3
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#fff" : "#555"
                    font.pixelSize: 10
                    font.bold: parent.enabled
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }

            // --- Separator ---
            Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

            // --- Empty state ---
            Text
            {
                visible: spatialController.focusPoints.length === 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "No focus points yet.\nClick + Add or Shift+click in the 3D view."
                color: "#666"; font.pixelSize: 11
                wrapMode: Text.Wrap
                topPadding: 20
                bottomPadding: 20
            }

            // --- List ---
            Repeater
            {
                model: spatialController.focusPoints

                delegate: Rectangle
                {
                    Layout.fillWidth: true
                    implicitHeight: rowContent.implicitHeight + 12
                    color: modelData.selected ? "#384050" : "#2e2e2e"
                    border.color: modelData.selected ? "#4a9eff" : "#3a3a3a"
                    border.width: 1
                    radius: 4

                    MouseArea
                    {
                        anchors.fill: parent
                        onClicked: spatialController.setSelectedFocusPointId(modelData.id)
                    }

                    ColumnLayout
                    {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4

                        // Name row: dot indicator, editable name, assignment badge, buttons
                        RowLayout
                        {
                            Layout.fillWidth: true
                            spacing: 4

                            // Selected indicator
                            Rectangle {
                                Layout.preferredWidth: 8; Layout.preferredHeight: 8
                                radius: 4
                                color: modelData.selected ? "#4a9eff" : "#555"
                            }

                            // Editable name — double-click to edit
                            TextInput
                            {
                                id: nameInput
                                Layout.fillWidth: true
                                text: modelData.name
                                color: "#e0e0e0"
                                font.pixelSize: 12
                                font.bold: modelData.selected
                                readOnly: true
                                selectByMouse: true
                                clip: true

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: nameInput.readOnly
                                    onDoubleClicked: {
                                        nameInput.readOnly = false
                                        nameInput.forceActiveFocus()
                                        nameInput.selectAll()
                                    }
                                    onClicked: spatialController.setSelectedFocusPointId(modelData.id)
                                }

                                onEditingFinished: {
                                    readOnly = true
                                    if (text !== modelData.name && text.length > 0)
                                        spatialController.renameFocusPoint(modelData.id, text)
                                    else
                                        text = modelData.name
                                }
                                Keys.onEscapePressed: {
                                    text = modelData.name
                                    readOnly = true
                                    focus = false
                                }
                            }

                            // Assignment badge
                            Rectangle {
                                visible: modelData.assignedCount > 0
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 18
                                radius: 9
                                color: "#444"
                                Text {
                                    anchors.centerIn: parent
                                    text: "[" + modelData.assignedCount + "]"
                                    color: "#ccc"; font.pixelSize: 9
                                }
                            }

                            // Aim button — IK all assigned fixtures to the point
                            Button
                            {
                                text: "\u25B6"  // ▶
                                implicitWidth: 24; implicitHeight: 22
                                enabled: modelData.assignedCount > 0
                                onClicked: spatialController.aimAtFocusPoint(modelData.id)
                                ToolTip.visible: hovered; ToolTip.delay: 500
                                ToolTip.text: modelData.assignedCount > 0
                                              ? "Aim assigned fixtures at this point"
                                              : "Assign fixtures first"
                                background: Rectangle {
                                    color: parent.enabled ? (parent.hovered ? "#2ecc71" : "#27ae60") : "#333"
                                    radius: 3
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled ? "#fff" : "#555"
                                    font.pixelSize: 10
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            // Delete
                            Button
                            {
                                text: "\u00d7"
                                implicitWidth: 22; implicitHeight: 22
                                onClicked: spatialController.deleteFocusPoint(modelData.id)
                                background: Rectangle { color: parent.hovered ? "#a33" : "transparent"; radius: 3 }
                                contentItem: Text { text: parent.text; color: "#c66"; font.pixelSize: 13
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            }
                        }

                        // Position row: X/Y/Z in meters
                        RowLayout
                        {
                            Layout.fillWidth: true
                            spacing: 4

                            Text { text: "X"; color: "#e74c3c"; font.pixelSize: 10; Layout.preferredWidth: 10 }
                            CompactSpin {
                                Layout.fillWidth: true
                                from: -100000; to: 100000; stepSize: 10
                                value: Math.round(modelData.x * 100)
                                editable: true
                                textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                                valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                                onValueModified: spatialController.moveFocusPoint(
                                    modelData.id, value / 100.0, modelData.y, modelData.z)
                            }

                            Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 10; Layout.preferredWidth: 10 }
                            CompactSpin {
                                Layout.fillWidth: true
                                from: -100000; to: 100000; stepSize: 10
                                value: Math.round(modelData.y * 100)
                                editable: true
                                textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                                valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                                onValueModified: spatialController.moveFocusPoint(
                                    modelData.id, modelData.x, value / 100.0, modelData.z)
                            }

                            Text { text: "Z"; color: "#3498db"; font.pixelSize: 10; Layout.preferredWidth: 10 }
                            CompactSpin {
                                Layout.fillWidth: true
                                from: -100000; to: 100000; stepSize: 10
                                value: Math.round(modelData.z * 100)
                                editable: true
                                textFromValue: function(v) { return (v / 100.0).toFixed(2) }
                                valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                                onValueModified: spatialController.moveFocusPoint(
                                    modelData.id, modelData.x, modelData.y, value / 100.0)
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }  // spacer so list stays at top

        } // end Focus mode ColumnLayout
        } // end ScrollView wrapping Focus

        // (No spacer needed — ScrollViews above use Layout.fillHeight)

        // ===============================================================
        // ALWAYS-VISIBLE BOTTOM: Snap + Camera
        // ===============================================================
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        // Snap + Camera combined (compact footer row)
        RowLayout
        {
            Layout.fillWidth: true
            spacing: 4

            Button
            {
                text: "\u229E"  // ⊞ grid symbol
                checkable: true
                checked: spatialController.gridSnap
                implicitWidth: 26; implicitHeight: 22
                onClicked: spatialController.gridSnap = checked
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: "Grid snap (" + (spatialController.gridSnap ? "ON" : "OFF") + ")"
                background: Rectangle { color: parent.checked ? "#4a9eff" : (parent.hovered ? "#444" : "#333"); radius: 3 }
                contentItem: Text { text: parent.text; color: parent.checked ? "#fff" : "#aaa";
                    font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }

            CompactSpin
            {
                id: gridSizeSpin
                from: 1; to: 500
                value: Math.round(spatialController.gridSize * 100)
                stepSize: 5
                Layout.preferredWidth: 80
                implicitHeight: 22
                editable: true
                enabled: spatialController.gridSnap
                onValueModified: spatialController.gridSize = value / 100.0
                textFromValue: function(v) { return (v / 100.0).toFixed(2) + "m" }
                valueFromText: function(t) { return Math.round(parseFloat(t) * 100) }
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: "Grid size (meters)"

                Connections
                {
                    target: spatialController
                    function onGridSizeChanged()
                    {
                        gridSizeSpin.value = Math.round(spatialController.gridSize * 100)
                    }
                }
            }

            // Separator
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#444" }

            // Camera presets
            Repeater
            {
                model: [
                    {"label": "FOH", "tip": "Front of House camera"},
                    {"label": "Top", "tip": "Top-down camera"},
                    {"label": "Frt", "tip": "Front camera"},
                    {"label": "Sid", "tip": "Side camera"}
                ]

                Button
                {
                    Layout.fillWidth: true
                    implicitHeight: 22
                    text: modelData.label
                    onClicked: spatialController.setCameraPreset(
                        modelData.label === "Frt" ? "Front"
                        : modelData.label === "Sid" ? "Side"
                        : modelData.label)
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: modelData.tip
                    background: Rectangle { color: parent.hovered ? "#444" : "#333"; radius: 3 }
                    contentItem: Text { text: parent.text; color: "#aaa"; font.pixelSize: 10;
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
    }
}
