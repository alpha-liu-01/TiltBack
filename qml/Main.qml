import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    visibility: Window.Maximized
    title: qsTr("TiltBack")
    color: "#1b1b1b"

    readonly property int cornerSize: Math.max(96, Math.round(Math.min(width, height) * 0.18))

    component CornerMark: Rectangle {
        width: root.cornerSize
        height: root.cornerSize
        color: "#2d4a6f"
        radius: 12

        property alias mark: label.text

        Label {
            id: label
            anchors.centerIn: parent
            color: "#f2f2f2"
            font.pixelSize: Math.max(22, Math.round(root.cornerSize * 0.28))
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#f2f2f2"
        font.pixelSize: Math.max(28, Math.round(root.width * 0.045))
        text: qsTr("TiltBack")
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.verticalCenter
        anchors.topMargin: Math.max(36, Math.round(root.height * 0.06))
        width: parent.width * 0.8
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#b0b0b0"
        font.pixelSize: Math.max(18, Math.round(root.width * 0.025))
        text: qsTr("Phase 0 — no backends")
    }

    CornerMark {
        mark: qsTr("TL")
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 16
    }

    CornerMark {
        mark: qsTr("TR")
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
    }

    CornerMark {
        mark: qsTr("BL")
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.margins: 16
    }

    CornerMark {
        mark: qsTr("BR")
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 16
    }
}
