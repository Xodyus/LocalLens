import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: "LocalLens"
    color: "#12141a"

    Label {
        anchors.centerIn: parent
        text: "LocalLens — scaffold running"
        color: "#e6e9ef"
        font.pixelSize: 22
    }
}
