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

    // 1,284 → "1,284" · 12900 → "12.9K" (stat-tile auto-compact values)
    function compact(n) {
        if (n >= 1e6) return (n / 1e6).toFixed(1) + "M"
        if (n >= 1e4) return (n / 1e3).toFixed(1) + "K"
        return Number(n).toLocaleString(Qt.locale(), 'f', 0)
    }

    AppController {
        id: app
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
                // later: keyboard navigation — Keys.onDownPressed moves
                // focus into the suggestion list; Esc closes the popup.
            }

            Button {
                text: "Index folder…"
                onClicked: folderDialog.open()
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

            ScrollBar.vertical: ScrollBar {}

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
                    // later: snippet line — needs SnippetRole in
                    // SearchResultModel (see the note there).
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

                // later: open the file on double-click
                // (Qt.openUrlExternally("file:///" + path)) and add a
                // right-click "Reveal in Explorer" menu.
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

            // later: query-latency sparkline tile — keep the last ~12
            // lastQueryMs values in a JS array (push on app.searchFinished)
            // and draw them with a Canvas or Shape in the de-emphasis hue.
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
            delegate: ItemDelegate {
                width: ListView.view.width
                highlighted: hovered

                contentItem: Label {
                    text: modelData
                    color: root.inkPrimary
                    font.pixelSize: 13
                }
                background: Rectangle {
                    color: highlighted ? root.surfaceBorder : "transparent"
                    radius: 4
                }
                onClicked: {
                    searchField.text = modelData
                    suggestionsPopup.close()
                    app.search(modelData)
                }
            }
        }
    }
}
