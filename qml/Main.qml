import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    visibility: Window.Maximized
    title: qsTr("TiltBack")
    color: "#1b1b1b"

    ClinicModel {
        id: clinic
    }

    readonly property int cornerSize: Math.max(72, Math.round(Math.min(width, height) * 0.12))

    component CornerMark: Rectangle {
        width: root.cornerSize
        height: root.cornerSize
        color: "#2d4a6f"
        radius: 12
        z: 20

        property alias mark: label.text

        Label {
            id: label
            anchors.centerIn: parent
            color: "#f2f2f2"
            font.pixelSize: Math.max(20, Math.round(root.cornerSize * 0.28))
        }
    }

    component LayerCard: Rectangle {
        required property string title
        required property string value
        required property string detail
        required property string how
        required property string backend

        default property alias extra: extraCol.data

        width: list.width
        implicitHeight: col.implicitHeight + 32
        color: "#242424"
        radius: 16
        border.color: "#3a3a3a"
        border.width: 1

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 8

            Label {
                width: parent.width
                text: title
                color: "#8ec8ff"
                font.pixelSize: Math.max(22, Math.round(root.width * 0.028))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                text: value
                color: "#f2f2f2"
                font.pixelSize: Math.max(26, Math.round(root.width * 0.036))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                text: detail
                color: "#d0d0d0"
                font.pixelSize: Math.max(18, Math.round(root.width * 0.022))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                text: qsTr("How: %1").arg(how)
                color: "#9a9a9a"
                font.pixelSize: Math.max(16, Math.round(root.width * 0.018))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                text: qsTr("Backend can change: %1").arg(backend)
                color: "#9a9a9a"
                font.pixelSize: Math.max(16, Math.round(root.width * 0.018))
                wrapMode: Text.WordWrap
            }

            Column {
                id: extraCol
                width: parent.width
                spacing: 8
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: root.cornerSize + 24
        anchors.bottomMargin: root.cornerSize + 24
        contentWidth: width
        contentHeight: list.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: list
            width: flick.width
            spacing: 14

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("TiltBack")
                color: "#f2f2f2"
                font.pixelSize: Math.max(28, Math.round(root.width * 0.04))
            }

            LayerCard {
                title: qsTr("Picture")
                value: clinic.pictureValue
                detail: clinic.pictureDetail
                how: clinic.pictureSource
                backend: clinic.pictureBackend

                Row {
                    spacing: 12
                    width: parent.width

                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Left")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyPicture("left")
                    }
                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Right")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyPicture("right")
                    }
                }
            }
            LayerCard {
                title: qsTr("Finger")
                value: clinic.fingerValue
                detail: clinic.fingerDetail
                how: clinic.fingerSource
                backend: clinic.fingerBackend
            }
            LayerCard {
                title: qsTr("Pen")
                value: clinic.penValue
                detail: clinic.penDetail
                how: clinic.penSource
                backend: clinic.penBackend
            }
            LayerCard {
                title: qsTr("Arrow")
                value: clinic.arrowValue
                detail: clinic.arrowDetail
                how: clinic.arrowSource
                backend: clinic.arrowBackend
            }

            Row {
                spacing: 12
                width: parent.width

                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(64, Math.round(root.height * 0.08))
                    text: qsTr("Refresh")
                    font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                    onClicked: clinic.refresh()
                }
                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(64, Math.round(root.height * 0.08))
                    text: qsTr("Copy report")
                    font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                    onClicked: clinic.copyReport()
                }
            }
        }
    }

    Rectangle {
        visible: clinic.pendingRevert
        z: 10
        anchors.fill: parent
        anchors.margins: root.cornerSize + 8
        color: "#e6111111"
        radius: 20
        border.color: "#8ec8ff"
        border.width: 2

        Column {
            anchors.centerIn: parent
            width: parent.width - 32
            spacing: 16

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Keep this picture?")
                color: "#f2f2f2"
                font.pixelSize: Math.max(28, Math.round(root.width * 0.04))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: String(clinic.revertSecondsLeft)
                color: "#8ec8ff"
                font.pixelSize: Math.max(64, Math.round(root.width * 0.12))
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: clinic.pendingTransform
                color: "#d0d0d0"
                font.pixelSize: Math.max(22, Math.round(root.width * 0.028))
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 12
                width: parent.width

                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(72, Math.round(root.height * 0.1))
                    text: qsTr("Keep")
                    font.pixelSize: Math.max(22, Math.round(root.width * 0.028))
                    onClicked: clinic.keepPicture()
                }
                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(72, Math.round(root.height * 0.1))
                    text: qsTr("Revert")
                    font.pixelSize: Math.max(22, Math.round(root.width * 0.028))
                    onClicked: clinic.revertPicture()
                }
            }
        }
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
