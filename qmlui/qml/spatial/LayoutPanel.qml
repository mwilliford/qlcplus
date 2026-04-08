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
                    width: (root.width - 16 - 6) / 4  // fill width minus margins and spacing
                    height: 32
                    text: modelData
                    checkable: true
                    checked: index === 0
                    autoExclusive: true

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

        // --- Properties header ---
        Text
        {
            text: "Properties"
            color: "#ccc"
            font.pixelSize: 13
            font.bold: true
        }

        // --- Position ---
        Text { text: "Position"; color: "#999"; font.pixelSize: 11 }

        GridLayout
        {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 4
            rowSpacing: 4

            Text { text: "X"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -10000; to: 10000; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 5000; value: 50; Layout.preferredWidth: 70; editable: true }

            Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -10000; to: 10000; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 5000; value: 50; Layout.preferredWidth: 70; editable: true }

            Text { text: "Z"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -10000; to: 10000; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 5000; value: 5; Layout.preferredWidth: 70; editable: true }
        }

        // --- Rotation ---
        Text { text: "Rotation"; color: "#999"; font.pixelSize: 11 }

        GridLayout
        {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 4
            rowSpacing: 4

            Text { text: "P"; color: "#e74c3c"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -3600; to: 3600; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 1800; value: 150; Layout.preferredWidth: 70; editable: true }

            Text { text: "Y"; color: "#2ecc71"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -3600; to: 3600; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 1800; value: 50; Layout.preferredWidth: 70; editable: true }

            Text { text: "R"; color: "#3498db"; font.pixelSize: 11; Layout.preferredWidth: 14 }
            SpinBox { from: -3600; to: 3600; value: 0; Layout.fillWidth: true; editable: true }
            Text { text: "\u00b1"; color: "#888"; font.pixelSize: 11 }
            SpinBox { from: 1; to: 1800; value: 50; Layout.preferredWidth: 70; editable: true }
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
                checked: true
                width: 60; height: 28

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
                text: "Plane"
                checkable: true
                width: 60; height: 28

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
                checked: true
                autoExclusive: true
                width: 60; height: 28

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
                autoExclusive: true
                width: 60; height: 28

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
