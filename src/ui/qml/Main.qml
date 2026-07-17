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

    AppController {
        id: app
    }

    FolderDialog {
        id: folderDialog
        onAccepted: app.indexFolder(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search your documents…"
                color: "#e6e9ef"
                placeholderTextColor: "#5c6370"
                background: Rectangle {
                    color: "#1b1e27"
                    radius: 6
                    border.color: searchField.activeFocus ? "#4c7dff" : "#2a2e3a"
                }
                onAccepted: app.search(text)
                // TODO(you): search-as-you-type with a debounce Timer, plus an
                // autocomplete Popup fed by IndexStore::suggestTerms().
            }

            Button {
                text: "Index folder…"
                onClicked: folderDialog.open()
            }
        }

        // TODO(you): replace this bare path list with a results view backed by
        // a QAbstractListModel exposing score / matched terms / a text snippet.
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: app.results
            delegate: Label {
                width: ListView.view.width
                text: modelData
                color: "#e6e9ef"
                elide: Text.ElideMiddle
            }
        }

        // TODO(you): turn this into real metric cards, then charts (step 3/5).
        Label {
            Layout.fillWidth: true
            color: "#8b93a7"
            font.pixelSize: 13
            elide: Text.ElideRight
            text: app.documentCount + " docs · " + app.termCount + " terms · "
                  + (app.databaseSizeBytes / 1024).toFixed(0) + " KB · last query "
                  + app.lastQueryMs.toFixed(2) + " ms — " + app.status
        }
    }
}
