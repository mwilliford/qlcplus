/*
  Q Light Controller Plus
  FixtureBrowser.qml

  Copyright (c) Massimo Callegari

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

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    anchors.fill: parent
    color: "transparent"

    property int manufacturerIndex: fixtureBrowser.manufacturerIndex
    property string selectedModel

    CustomPopupDialog
    {
        id: errorPopup
        standardButtons: Dialog.Ok
        title: qsTr("Error")
        message: qsTr("Address overlapping detected.\nPlease set another DMX address.")
        onAccepted: close()
    }

    RowLayout
    {
        id: toolBar
        z: 1
        width: parent.width
        height: UISettings.iconSizeMedium

        Rectangle
        {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: UISettings.bgMedium
            radius: 5
            border.width: 2
            border.color: UISettings.borderColorDark

            Text
            {
                id: searchIcon
                x: 6
                width: height
                height: parent.height - 6
                anchors.verticalCenter: parent.verticalCenter
                color: "gray"
                font.family: UISettings.fontAwesomeFontName
                font.pixelSize: height - 6
                text: FontAwesome.fa_magnifying_glass
            }

            TextInput
            {
                x: searchIcon.width + 14
                y: 3
                height: parent.height - 6
                width: parent.width - x
                color: UISettings.fgMain
                text: fixtureBrowser.searchFilter
                font.family: UISettings.robotoFontName
                font.pixelSize: height - 6
                selectionColor: UISettings.highlightPressed
                selectByMouse: true

                onTextEdited: fixtureBrowser.searchFilter = text
            }
        }

        IconButton
        {
            width: height
            height: toolBar.height - 2
            faSource: FontAwesome.fa_plus
            faColor: "limegreen"
            tooltip: qsTr("Create a new fixture definition")
            onClicked: qlcplus.createFixture()
        }

        IconButton
        {
            id: editButton
            enabled: fixtureBrowser.selectedModel.length ? true : false
            width: height
            height: toolBar.height - 2
            imgSource: "qrc:/edit.svg"
            tooltip: qsTr("Edit the selected fixture definition")
            onClicked: qlcplus.editFixture(fixtureBrowser.selectedManufacturer, fixtureBrowser.selectedModel)
        }
    }

    // Source toggle: Local / GDTF-share
    Row
    {
        id: sourceToggle
        z: 2
        height: UISettings.listItemHeight
        anchors.top: toolBar.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2

        Rectangle
        {
            width: 100
            height: UISettings.listItemHeight
            radius: 3
            color: !fixtureBrowser.gdtfShareSource ? UISettings.highlight : UISettings.bgMedium
            border.width: 1
            border.color: UISettings.borderColorDark

            RobotoText
            {
                anchors.centerIn: parent
                label: qsTr("Local")
                fontSize: UISettings.textSizeDefault
                labelColor: !fixtureBrowser.gdtfShareSource ? "white" : UISettings.fgMedium
            }
            MouseArea
            {
                anchors.fill: parent
                onClicked: fixtureBrowser.gdtfShareSource = false
            }
        }

        Rectangle
        {
            width: 100
            height: UISettings.listItemHeight
            radius: 3
            color: fixtureBrowser.gdtfShareSource ? UISettings.highlight : UISettings.bgMedium
            border.width: 1
            border.color: UISettings.borderColorDark

            RobotoText
            {
                anchors.centerIn: parent
                label: qsTr("GDTF-share")
                fontSize: UISettings.textSizeDefault
                labelColor: fixtureBrowser.gdtfShareSource ? "white" : UISettings.fgMedium
            }
            MouseArea
            {
                anchors.fill: parent
                onClicked: fixtureBrowser.gdtfShareSource = true
            }
        }
    }

    // -----------------------------------------------------------------------
    // LOCAL fixture views (existing)
    // -----------------------------------------------------------------------

    ListView
    {
        id: manufacturerList
        visible: !fixtureBrowser.gdtfShareSource && fixtureBrowser.selectedManufacturer.length === 0 && fixtureBrowser.searchFilter.length < 3
        z: 0
        width: parent.width - 12
        height: parent.height - toolBar.height - sourceToggle.height - 16
        anchors.top: sourceToggle.bottom
        anchors.topMargin: 4
        anchors.margins: 6
        focus: true

        boundsBehavior: Flickable.StopAtBounds
        currentIndex: manufacturerIndex

        highlight: Component
        {
            Rectangle
            {
                y: manufacturerList.currentItem.y
                width: parent.width
                height: UISettings.listItemHeight
                color: UISettings.highlight
            }
        }
        highlightFollowsCurrentItem: false

        model: fixtureBrowser.manufacturers
        delegate:
            FixtureBrowserDelegate
            {
                width: modelsList.width - (manufScroll.visible ? manufScroll.width : 0)
                isManufacturer: true
                textLabel: modelData
                onMouseEvent: (type, iID, iType, qItem, mouseMods) =>
                {
                    if (type === App.Clicked)
                    {
                        mfText.label = modelData
                        fixtureBrowser.manufacturerIndex = index
                        fixtureBrowser.selectedManufacturer = modelData
                        modelsList.currentIndex = -1
                    }
                }
            }

        Component.onCompleted: manufacturerList.positionViewAtIndex(manufacturerIndex, ListView.Center)

        ScrollBar.vertical: CustomScrollBar { id: manufScroll }
    }

    Rectangle
    {
        id: fixtureArea
        visible: !fixtureBrowser.gdtfShareSource && fixtureBrowser.selectedManufacturer.length && fixtureBrowser.searchFilter.length < 3
        color: "transparent"

        width: parent.width
        height: parent.height - toolBar.height - sourceToggle.height - 16 - (fxPropsRect.visible ? fxPropsRect.height : 0)
        anchors.top: sourceToggle.bottom
        anchors.topMargin: 4
        anchors.margins: 6

        Rectangle
        {
            id: manufBackLink
            height: UISettings.iconSizeMedium
            z: 1
            anchors.right: parent.right
            anchors.left: parent.left
            color: blMouseArea.pressed ? UISettings.bgLight : UISettings.bgMedium

            // left arrow
            Text
            {
                id: leftArrow
                anchors.left: parent.left
                anchors.leftMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                color: UISettings.fgLight
                font.family: UISettings.fontAwesomeFontName
                font.pixelSize: parent.height - 8
                text: FontAwesome.fa_chevron_left
            }

            RobotoText
            {
                id: mfText
                anchors.left: leftArrow.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                fontSize: UISettings.textSizeDefault
                fontBold: true
                labelColor: UISettings.fgMedium
            }
            MouseArea
            {
                id: blMouseArea
                anchors.fill: parent
                hoverEnabled: true

                onClicked:
                {
                    fxPropsRect.visible = false
                    panelPropsRect.visible = false
                    fixtureBrowser.selectedManufacturer = ""
                    editButton.enabled = false
                }
            }
        }

        ListView
        {
            id: modelsList
            z: 0

            width: parent.width - 12
            height: parent.height - manufBackLink.height - 12
            anchors.top: manufBackLink.bottom
            anchors.margins: 6

            focus: true
            boundsBehavior: Flickable.StopAtBounds
            highlight:
                Rectangle
                {
                    width: modelsList.width
                    height: UISettings.listItemHeight - 2
                    color: UISettings.highlight
                    y: modelsList.currentItem ? modelsList.currentItem.y + 1 : 0
                }
            highlightFollowsCurrentItem: false

            model: fixtureBrowser.modelsList
            delegate:
                FixtureBrowserDelegate
                {
                    id: dlg
                    width: modelsList.width - (modelsScroll.visible ? modelsScroll.width : 0)
                    manufacturer: fixtureBrowser.selectedManufacturer
                    textLabel: modelData

                    onMouseEvent: (type, iID, iType, qItem, mouseMods) =>
                    {
                        if (type === App.Clicked)
                        {
                            modelsList.currentIndex = index
                            fixtureBrowser.selectedModel = modelData
                            if (modelData == "Generic RGB Panel")
                            {
                                fxPropsRect.visible = false
                                panelPropsRect.visible = true
                            }
                            else
                            {
                                panelPropsRect.visible = false
                                fxPropsRect.visible = true
                            }
                            editButton.enabled = true
                        }
                    }
                    onOverlappingEvent: errorPopup.open()
                }
            ScrollBar.vertical: CustomScrollBar { id: modelsScroll }
        }
    }

    Flickable
    {
        id: searchRect
        clip: true
        visible: !fixtureBrowser.gdtfShareSource && fixtureBrowser.searchFilter.length >= 3 ? true : false
        boundsBehavior: Flickable.StopAtBounds

        contentHeight: searchColumn.height

        width: parent.width
        height: parent.height - toolBar.height - sourceToggle.height - 16 - (fxPropsRect.visible ? fxPropsRect.height : 0)
        anchors.top: sourceToggle.bottom
        anchors.topMargin: 4
        anchors.margins: 6

        Column
        {
            id: searchColumn
            width: parent.width

            Repeater
            {
                id: searchListView
                anchors.fill: parent
                z: 4
                model: fixtureBrowser.searchTreeModel
                delegate:
                    Component
                    {
                        Loader
                        {
                            width: searchListView.width
                            source: hasChildren ? "qrc:/TreeNodeDelegate.qml" : "qrc:/FixtureBrowserDelegate.qml"
                            onLoaded:
                            {
                                if (hasChildren)
                                {
                                    item.textLabel = label
                                    item.nodePath = path
                                    item.itemIcon = ""
                                    item.isExpanded = true
                                    item.childrenDelegate = "qrc:/FixtureBrowserDelegate.qml"
                                    item.nodeChildren = childrenModel
                                }
                            }
                            Connections
                            {
                                target: item
                                function onMouseEvent(type, iID, iType, qItem, mouseMods)
                                {
                                    if (type === App.Clicked)
                                    {
                                        console.log("Item clicked with path: " + item.nodePath + "/" + qItem.textLabel)
                                        model.isSelected = 2
                                        qItem.manufacturer = item.nodePath
                                        fixtureBrowser.selectedManufacturer = qItem.manufacturer
                                        fixtureBrowser.selectedModel = qItem.textLabel
                                        fxPropsRect.visible = true
                                        editButton.enabled = true
                                    }
                                }
                            }
                        }
                    }
            } // end of Repeater
        } // end of Column

        ScrollBar.vertical: CustomScrollBar { }
    } // end of Flickable

    // -----------------------------------------------------------------------
    // GDTF-share view
    // -----------------------------------------------------------------------

    Rectangle
    {
        id: gdtfShareView
        visible: fixtureBrowser.gdtfShareSource
        color: "transparent"

        width: parent.width
        height: parent.height - toolBar.height - sourceToggle.height - 16
        anchors.top: sourceToggle.bottom
        anchors.topMargin: 4
        anchors.margins: 6

        // Login section (shown when not logged in)
        Column
        {
            id: loginSection
            visible: !fixtureBrowser.gdtfShareLoggedIn && !fixtureBrowser.gdtfShareLoading
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 8

            RobotoText
            {
                label: qsTr("Login to GDTF-share")
                fontSize: UISettings.textSizeDefault
                fontBold: true
            }

            Rectangle
            {
                width: parent.width
                height: UISettings.listItemHeight
                color: UISettings.bgMedium
                radius: 3
                border.width: 1
                border.color: UISettings.borderColorDark

                TextInput
                {
                    id: gdtfUsername
                    anchors.fill: parent
                    anchors.margins: 4
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: height - 8
                    clip: true
                    activeFocusOnTab: true
                    KeyNavigation.tab: gdtfPassword
                    Text
                    {
                        visible: !gdtfUsername.text.length && !gdtfUsername.activeFocus
                        text: qsTr("Username")
                        color: "gray"
                        font: gdtfUsername.font
                    }
                }
            }

            Rectangle
            {
                width: parent.width
                height: UISettings.listItemHeight
                color: UISettings.bgMedium
                radius: 3
                border.width: 1
                border.color: UISettings.borderColorDark

                TextInput
                {
                    id: gdtfPassword
                    anchors.fill: parent
                    anchors.margins: 4
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: height - 8
                    echoMode: TextInput.Password
                    clip: true
                    activeFocusOnTab: true
                    Keys.onReturnPressed: fixtureBrowser.gdtfShareLogin(gdtfUsername.text, gdtfPassword.text)
                    Keys.onEnterPressed: fixtureBrowser.gdtfShareLogin(gdtfUsername.text, gdtfPassword.text)
                    Text
                    {
                        visible: !gdtfPassword.text.length && !gdtfPassword.activeFocus
                        text: qsTr("Password")
                        color: "gray"
                        font: gdtfPassword.font
                    }
                }
            }

            GenericButton
            {
                width: parent.width
                height: UISettings.listItemHeight
                label: qsTr("Login")
                onClicked: (mouseButton) => fixtureBrowser.gdtfShareLogin(gdtfUsername.text, gdtfPassword.text)
            }

            // Error message
            RobotoText
            {
                visible: fixtureBrowser.gdtfShareError.length > 0
                label: fixtureBrowser.gdtfShareError
                labelColor: "red"
                fontSize: UISettings.textSizeDefault
                wrapText: true
                width: parent.width
            }
        }

        // Loading indicator
        RobotoText
        {
            visible: fixtureBrowser.gdtfShareLoading
            anchors.centerIn: parent
            label: qsTr("Loading...")
            fontSize: UISettings.textSizeDefault
        }

        // Fixture list (shown when logged in and list loaded)
        Column
        {
            id: gdtfShareListSection
            visible: fixtureBrowser.gdtfShareLoggedIn && !fixtureBrowser.gdtfShareLoading
            anchors.fill: parent
            spacing: 4

            // Fixture count + refresh
            Row
            {
                width: parent.width
                spacing: 8

                RobotoText
                {
                    label: fixtureBrowser.gdtfShareFixtures.length + qsTr(" fixtures")
                    fontSize: UISettings.textSizeDefault - 2
                    labelColor: UISettings.fgMedium
                    anchors.verticalCenter: parent.verticalCenter
                }

                IconButton
                {
                    width: UISettings.iconSizeDefault
                    height: UISettings.iconSizeDefault
                    faSource: FontAwesome.fa_rotate
                    faColor: UISettings.fgMedium
                    tooltip: qsTr("Refresh fixture list")
                    onClicked: fixtureBrowser.gdtfShareFetchList()
                }
            }

            // Error banner
            RobotoText
            {
                visible: fixtureBrowser.gdtfShareError.length > 0
                label: fixtureBrowser.gdtfShareError
                labelColor: "red"
                fontSize: UISettings.textSizeDefault
                wrapText: true
                width: parent.width
            }

            // Fixture list
            ListView
            {
                id: gdtfShareList
                width: parent.width
                height: parent.height - y
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: fixtureBrowser.gdtfShareFixtures

                delegate: Rectangle
                {
                    required property var modelData
                    required property int index

                    width: gdtfShareList.width - (gdtfShareScroll.visible ? gdtfShareScroll.width : 0)
                    height: fxCol.implicitHeight + 8
                    clip: true
                    color: gdtfDelegateMA.containsMouse ? UISettings.highlight : "transparent"

                    property string fxMfg: modelData.manufacturer || ""
                    property string fxName: modelData.name || ""
                    property string fxRid: modelData.rid || ""
                    property string fxRev: modelData.revision || ""
                    property string fxRating: modelData.rating || ""

                    Column
                    {
                        id: fxCol
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.right: gdtfDownloadBtn.left
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        RobotoText
                        {
                            label: fxMfg + " — " + fxName
                            fontSize: UISettings.textSizeDefault
                            width: parent.width
                        }
                        RobotoText
                        {
                            visible: fxRev.length > 0
                            label: fxRev + (fxRating.length > 0 ? "  ★ " + fxRating : "")
                            fontSize: UISettings.textSizeDefault - 3
                            labelColor: UISettings.fgMedium
                            width: parent.width
                        }
                    }

                    IconButton
                    {
                        id: gdtfDownloadBtn
                        width: UISettings.iconSizeMedium
                        height: UISettings.iconSizeMedium
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        faSource: FontAwesome.fa_download
                        faColor: UISettings.fgMain
                        tooltip: qsTr("Download")
                        onClicked: fixtureBrowser.gdtfShareDownload(fxRid, fxMfg, fxName)
                    }

                    MouseArea
                    {
                        id: gdtfDelegateMA
                        anchors.fill: parent
                        hoverEnabled: true
                        propagateComposedEvents: true
                        onClicked: (mouse) => mouse.accepted = false
                        onPressed: (mouse) => mouse.accepted = false
                        onReleased: (mouse) => mouse.accepted = false
                    }
                }

                ScrollBar.vertical: CustomScrollBar { id: gdtfShareScroll }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Properties panels (local fixtures only)
    // -----------------------------------------------------------------------

    FixtureProperties
    {
        id: fxPropsRect
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        visible: false
    }

    RGBPanelProperties
    {
        objectName: "RGBPanelProps"
        id: panelPropsRect
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        visible: false
    }
}
