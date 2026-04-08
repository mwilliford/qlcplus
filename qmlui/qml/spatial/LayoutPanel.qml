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

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

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
                    height: 32
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

        // --- Separator ---
        Rectangle { Layout.fillWidth: true; height: 1; color: "#444" }

        // --- Selection header ---
        Text
        {
            text: spatialController.hasSelection
                  ? spatialController.selectedFixtureName
                  : "No Selection"
            color: spatialController.hasSelection ? "#ccc" : "#666"
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        // --- Properties (visible only when a fixture is selected) ---
        ColumnLayout
        {
            visible: spatialController.hasSelection
            Layout.fillWidth: true
            spacing: 6

            // --- Position ---
            Text { text: "Position (m)"; color: "#999"; font.pixelSize: 11 }

            GridLayout
            {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 4
                rowSpacing: 4

                Text { text: "X"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: posXSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posX * 100)
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.posX = value / 100.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 100.0).toFixed(2)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 100)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
                            posXSpin.updating = true
                            posXSpin.value = Math.round(spatialController.posX * 100)
                            posXSpin.updating = false
                        }
                    }
                }

                Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: posYSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posY * 100)
                    stepSize: 10
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.posY = value / 100.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 100.0).toFixed(2)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 100)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
                            posYSpin.updating = true
                            posYSpin.value = Math.round(spatialController.posY * 100)
                            posYSpin.updating = false
                        }
                    }
                }

                Text { text: "Z"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: posZSpin
                    from: -100000; to: 100000
                    value: Math.round(spatialController.posZ * 100)
                    stepSize: 1
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.posZ = value / 100.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 100.0).toFixed(2)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 100)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
                            posZSpin.updating = true
                            posZSpin.value = Math.round(spatialController.posZ * 100)
                            posZSpin.updating = false
                        }
                    }
                }
            }

            // --- Rotation ---
            Text { text: "Rotation (\u00B0)"; color: "#999"; font.pixelSize: 11 }

            GridLayout
            {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 4
                rowSpacing: 4

                Text { text: "P"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: rotPSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotPitch * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.rotPitch = value / 10.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 10.0).toFixed(1)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 10)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
                            rotPSpin.updating = true
                            rotPSpin.value = Math.round(spatialController.rotPitch * 10)
                            rotPSpin.updating = false
                        }
                    }
                }

                Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: rotYSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotYaw * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.rotYaw = value / 10.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 10.0).toFixed(1)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 10)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
                            rotYSpin.updating = true
                            rotYSpin.value = Math.round(spatialController.rotYaw * 10)
                            rotYSpin.updating = false
                        }
                    }
                }

                Text { text: "R"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 14 }
                SpinBox
                {
                    id: rotRSpin
                    from: -36000; to: 36000
                    value: Math.round(spatialController.rotRoll * 10)
                    stepSize: 50
                    Layout.fillWidth: true
                    editable: true

                    property bool updating: false

                    onValueModified:
                    {
                        if (!updating)
                            spatialController.rotRoll = value / 10.0
                    }

                    textFromValue: function(value, locale) {
                        return (value / 10.0).toFixed(1)
                    }
                    valueFromText: function(text, locale) {
                        return Math.round(parseFloat(text) * 10)
                    }

                    Connections
                    {
                        target: spatialController
                        function onTransformChanged()
                        {
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
        Text
        {
            text: "Snap"
            color: "#ccc"
            font.pixelSize: 13
            font.bold: true
        }

        Row
        {
            spacing: 8

            Button
            {
                text: "Grid"
                checkable: true
                checked: spatialController.gridSnap
                width: 60; height: 28

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
        }

        // --- Axis mode ---
        Row
        {
            spacing: 8

            Text { text: "Axes:"; color: "#999"; font.pixelSize: 11;
                   anchors.verticalCenter: parent.verticalCenter }

            Button
            {
                text: "World"
                checkable: true
                checked: spatialController.axisMode === 0
                autoExclusive: true
                width: 60; height: 28

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
                width: 60; height: 28

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

        Text
        {
            text: "Camera"
            color: "#ccc"
            font.pixelSize: 13
            font.bold: true
        }

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
