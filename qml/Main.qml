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

        width: parent.width
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

    component RevertBanner: Rectangle {
        required property string question
        required property int seconds
        required property string detail
        signal keepClicked()
        signal revertClicked()

        width: parent.width
        implicitHeight: visible ? bannerCol.implicitHeight + 28 : 0
        height: implicitHeight
        color: "#e6111111"
        radius: 16
        border.color: "#8ec8ff"
        border.width: 2

        Column {
            id: bannerCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: question
                color: "#f2f2f2"
                font.pixelSize: Math.max(24, Math.round(root.width * 0.032))
                wrapMode: Text.WordWrap
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: String(seconds)
                color: "#8ec8ff"
                font.pixelSize: Math.max(40, Math.round(root.width * 0.07))
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: detail
                color: "#d0d0d0"
                font.pixelSize: Math.max(18, Math.round(root.width * 0.022))
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 12
                width: parent.width

                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(64, Math.round(root.height * 0.08))
                    text: qsTr("Keep")
                    font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                    onClicked: keepClicked()
                }
                Button {
                    width: (parent.width - parent.spacing) / 2
                    height: Math.max(64, Math.round(root.height * 0.08))
                    text: qsTr("Revert")
                    font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                    onClicked: revertClicked()
                }
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
                        text: qsTr("None")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyPicture("none")
                    }
                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Left")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyPicture("left")
                    }
                }
                Row {
                    spacing: 12
                    width: parent.width

                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Inverted")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyPicture("inverted")
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

                Flow {
                    id: fingerR
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: [
                            { r: 0, label: qsTr("R=0 Primary") },
                            { r: 1, label: qsTr("R=1 Portrait") },
                            { r: 2, label: qsTr("R=2 Landscape") },
                            { r: 4, label: qsTr("R=4 Inv. Portrait") },
                            { r: 8, label: qsTr("R=8 Inv. Landscape") }
                        ]
                        Button {
                            width: (fingerR.width - 16) / 3
                            height: Math.max(64, Math.round(root.height * 0.08))
                            text: modelData.label
                            font.pixelSize: Math.max(16, Math.round(root.width * 0.018))
                            onClicked: clinic.applyFinger(modelData.r)
                        }
                    }
                }
            }
            LayerCard {
                title: qsTr("Pen")
                value: clinic.penValue
                detail: clinic.penDetail
                how: clinic.penSource
                backend: clinic.penBackend

                Flow {
                    id: penR
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: [
                            { r: 0, label: qsTr("R=0 Primary") },
                            { r: 1, label: qsTr("R=1 Portrait") },
                            { r: 2, label: qsTr("R=2 Landscape") },
                            { r: 4, label: qsTr("R=4 Inv. Portrait") },
                            { r: 8, label: qsTr("R=8 Inv. Landscape") }
                        ]
                        Button {
                            width: (penR.width - 16) / 3
                            height: Math.max(64, Math.round(root.height * 0.08))
                            text: modelData.label
                            font.pixelSize: Math.max(16, Math.round(root.width * 0.018))
                            onClicked: clinic.applyPen(modelData.r)
                        }
                    }
                }
            }
            LayerCard {
                title: qsTr("Arrow")
                value: clinic.arrowValue
                detail: clinic.arrowDetail
                how: clinic.arrowSource
                backend: clinic.arrowBackend
            }
            LayerCard {
                title: qsTr("Tilt")
                value: clinic.tiltValue
                detail: clinic.tiltDetail
                how: clinic.tiltSource
                backend: clinic.tiltBackend

                Row {
                    visible: clinic.tiltCanApply
                    spacing: 12
                    width: parent.width

                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Prev")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyTilt("prev")
                    }
                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Next")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyTilt("next")
                    }
                }
                Row {
                    visible: clinic.tiltCanApply
                    spacing: 12
                    width: parent.width

                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Identity")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyTilt("identity")
                    }
                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Wiki")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.applyTilt("wiki")
                    }
                }
            }
            LayerCard {
                title: qsTr("Home / Follow")
                value: clinic.homeLine
                detail: clinic.persistLine + "\n" + clinic.followStatus
                how: clinic.persistHow
                backend: qsTr("follow is the lock, not an apply button")

                Row {
                    spacing: 12
                    width: parent.width

                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Save home")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.saveHome()
                    }
                    Button {
                        width: (parent.width - parent.spacing) / 2
                        height: Math.max(64, Math.round(root.height * 0.08))
                        text: qsTr("Install/start")
                        font.pixelSize: Math.max(20, Math.round(root.width * 0.024))
                        onClicked: clinic.installFollow()
                    }
                }
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
        visible: clinic.pendingPictureRevert || clinic.pendingFingerRevert || clinic.pendingPenRevert || clinic.pendingTiltRevert
        z: 10
        anchors.fill: parent
        anchors.margins: root.cornerSize + 8
        color: "#99111111"
        radius: 20

        Flickable {
            anchors.fill: parent
            anchors.margins: 8
            contentWidth: width
            contentHeight: banners.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: banners
                width: parent.width
                spacing: 12

                RevertBanner {
                    visible: clinic.pendingPictureRevert
                    question: qsTr("Keep this picture?")
                    seconds: clinic.pictureSecondsLeft
                    detail: clinic.pendingPictureTransform
                    onKeepClicked: clinic.keepPicture()
                    onRevertClicked: clinic.revertPicture()
                }
                RevertBanner {
                    visible: clinic.pendingFingerRevert
                    question: qsTr("Keep this finger?")
                    seconds: clinic.fingerSecondsLeft
                    detail: clinic.pendingFingerTransform
                    onKeepClicked: clinic.keepFinger()
                    onRevertClicked: clinic.revertFinger()
                }
                RevertBanner {
                    visible: clinic.pendingPenRevert
                    question: qsTr("Keep this pen?")
                    seconds: clinic.penSecondsLeft
                    detail: clinic.pendingPenTransform
                    onKeepClicked: clinic.keepPen()
                    onRevertClicked: clinic.revertPen()
                }
                RevertBanner {
                    visible: clinic.pendingTiltRevert
                    question: qsTr("Keep this sensor reload?")
                    seconds: clinic.tiltSecondsLeft
                    detail: clinic.pendingTiltTransform
                    onKeepClicked: clinic.keepTilt()
                    onRevertClicked: clinic.revertTilt()
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
