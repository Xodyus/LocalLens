import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: "LocalLens"
    color: "#12141a"

    // Ink tokens — text never wears an accent/series color.
    readonly property color inkPrimary: "#e6e9ef"
    readonly property color inkSecondary: "#a7aebc"
    readonly property color inkMuted: "#7c8496"
    readonly property color surface: "#1b1e27"
    readonly property color surfaceBorder: "#2a2e3a"
    readonly property color accent: "#4c7dff"

    // Last ~12 query times, for the sparkline tile.
    property var queryHistory: []

    // 1,284 → "1,284" · 12900 → "12.9K" (stat-tile auto-compact values)
    function compact(n) {
        if (n >= 1e6) return (n / 1e6).toFixed(1) + "M"
        if (n >= 1e4) return (n / 1e3).toFixed(1) + "K"
        return Number(n).toLocaleString(Qt.locale(), 'f', 0)
    }

    function selectSuggestion(text) {
        searchField.text = text
        suggestionsPopup.close()
        app.search(text)
        searchField.forceActiveFocus()
    }

    AppController {
        id: app
    }

    Connections {
        target: app
        function onSearchFinished() {
            root.queryHistory.push(app.lastQueryMs)
            if (root.queryHistory.length > 12)
                root.queryHistory.shift()
            sparklineCanvas.requestPaint()
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: app.indexFolder(selectedFolder)
    }

    // Debounce: run search + refresh suggestions 250 ms after the last keystroke.
    Timer {
        id: searchDebounce
        interval: 250
        onTriggered: {
            app.search(searchField.text)
            suggestionList.model = app.suggest(searchField.text)
            suggestionsPopup.visible = searchField.activeFocus
                    && suggestionList.model.length > 0
        }
    }

    component MetricTile: Rectangle {
        property alias label: tileLabel.text
        property alias value: tileValue.text

        Layout.fillWidth: true
        implicitHeight: 72
        radius: 8
        color: root.surface
        border.color: root.surfaceBorder

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 2

            Label {
                id: tileLabel
                color: root.inkMuted
                font.pixelSize: 12
            }
            Label {
                id: tileValue
                color: root.inkPrimary
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ---- Search row ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search your documents…"
                color: root.inkPrimary
                placeholderTextColor: root.inkMuted
                background: Rectangle {
                    color: root.surface
                    radius: 6
                    border.color: searchField.activeFocus ? root.accent : root.surfaceBorder
                }
                onTextEdited: searchDebounce.restart()
                onAccepted: {
                    searchDebounce.stop()
                    suggestionsPopup.close()
                    app.search(text)
                }
                Keys.onDownPressed: {
                    if (suggestionsPopup.visible) {
                        suggestionList.currentIndex = 0
                        suggestionList.forceActiveFocus()
                    }
                }
                Keys.onEscapePressed: {
                    if (suggestionsPopup.visible)
                        suggestionsPopup.close()
                }
            }

            Button {
                text: "Index folder…"
                onClicked: folderDialog.open()
            }
        }

        // ---- Watched folders ----
        Flow {
            Layout.fillWidth: true
            visible: app.watchedFolders.length > 0
            spacing: 6

            Repeater {
                model: app.watchedFolders
                delegate: Rectangle {
                    radius: 4
                    color: root.surface
                    border.color: root.surfaceBorder
                    implicitWidth: chipRow.implicitWidth + 16
                    implicitHeight: chipRow.implicitHeight + 10

                    RowLayout {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6

                        Label {
                            text: modelData
                            color: root.inkSecondary
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 260
                        }
                        Label {
                            text: "✕"
                            color: root.inkMuted
                            font.pixelSize: 12

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4 // bigger hit target than the glyph
                                onClicked: app.removeFolder(modelData)
                            }
                        }
                    }
                }
            }
        }

        // ---- Results ----
        ListView {
            id: resultsView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: app.results

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }

            delegate: Rectangle {
                width: ListView.view.width
                implicitHeight: resultColumn.implicitHeight + 20
                radius: 8
                color: root.surface
                border.color: root.surfaceBorder

                ColumnLayout {
                    id: resultColumn
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: scoreColumn.left
                    anchors.margins: 12
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: fileName
                        color: root.inkPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: path
                        color: root.inkMuted
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: snippet
                        visible: snippet.length > 0
                        color: root.inkSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }
                }

                ColumnLayout {
                    id: scoreColumn
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: score.toFixed(2)
                        color: root.inkSecondary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: matchedTerms + (matchedTerms === 1 ? " term" : " terms")
                        color: root.inkMuted
                        font.pixelSize: 11
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onDoubleClicked: (mouse) => {
                        if (mouse.button === Qt.LeftButton)
                            Qt.openUrlExternally("file:///" + path)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            resultContextMenu.popup()
                    }
                }

                Menu {
                    id: resultContextMenu
                    MenuItem {
                        text: "Open"
                        onTriggered: Qt.openUrlExternally("file:///" + path)
                    }
                    MenuItem {
                        text: "Reveal in Explorer"
                        onTriggered: app.revealInExplorer(path)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: resultsView.count === 0
                text: app.documentCount === 0
                      ? "Index a folder to get started"
                      : "Type to search " + root.compact(app.documentCount) + " documents"
                color: root.inkMuted
                font.pixelSize: 14
            }
        }

        // ---- Metric tiles ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            MetricTile { label: "Documents"; value: root.compact(app.documentCount) }
            MetricTile { label: "Terms"; value: root.compact(app.termCount) }
            MetricTile { label: "Index size"; value: (app.databaseSizeBytes / 1048576).toFixed(1) + " MB" }
            MetricTile { label: "Queue"; value: root.compact(app.queueDepth) }
            MetricTile { label: "Last query"; value: app.lastQueryMs.toFixed(2) + " ms" }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 72
                radius: 8
                color: root.surface
                border.color: root.surfaceBorder

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2

                    Label {
                        text: "Query trend"
                        color: root.inkMuted
                        font.pixelSize: 12
                    }
                    Canvas {
                        id: sparklineCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const pts = root.queryHistory
                            if (pts.length < 2)
                                return
                            let minV = Math.min.apply(Math, pts)
                            let maxV = Math.max.apply(Math, pts)
                            if (maxV === minV)
                                maxV = minV + 1
                            ctx.strokeStyle = root.inkMuted
                            ctx.lineWidth = 2
                            ctx.beginPath()
                            for (let i = 0; i < pts.length; i++) {
                                const x = width * i / (pts.length - 1)
                                const y = height - ((pts[i] - minV) / (maxV - minV)) * height
                                if (i === 0) ctx.moveTo(x, y)
                                else ctx.lineTo(x, y)
                            }
                            ctx.stroke()

                            const lastY = height - ((pts[pts.length - 1] - minV) / (maxV - minV)) * height
                            ctx.fillStyle = root.accent
                            ctx.beginPath()
                            ctx.arc(width, lastY, 3, 0, 2 * Math.PI)
                            ctx.fill()
                        }
                    }
                }
            }
        }

        // ---- Status line ----
        Label {
            Layout.fillWidth: true
            color: root.inkMuted
            font.pixelSize: 12
            elide: Text.ElideRight
            text: app.status
        }
    }

    // ---- Autocomplete ----
    Popup {
        id: suggestionsPopup
        x: searchField.x + 16 // ColumnLayout margin offset
        y: searchField.y + searchField.height + 20
        width: searchField.width
        padding: 4
        background: Rectangle {
            color: root.surface
            radius: 6
            border.color: root.surfaceBorder
        }

        contentItem: ListView {
            id: suggestionList
            implicitHeight: Math.min(contentHeight, 240)
            clip: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0

            Keys.onUpPressed: {
                if (currentIndex <= 0)
                    searchField.forceActiveFocus()
                else
                    decrementCurrentIndex()
            }
            Keys.onDownPressed: incrementCurrentIndex()
            Keys.onReturnPressed: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    root.selectSuggestion(model[currentIndex])
            }
            Keys.onEscapePressed: {
                suggestionsPopup.close()
                searchField.forceActiveFocus()
            }

            delegate: ItemDelegate {
                width: ListView.view.width
                highlighted: hovered || ListView.isCurrentItem

                contentItem: Label {
                    text: modelData
                    color: root.inkPrimary
                    font.pixelSize: 13
                }
                background: Rectangle {
                    color: highlighted ? root.surfaceBorder : "transparent"
                    radius: 4
                }
                onClicked: root.selectSuggestion(modelData)
            }
        }
    }
}
