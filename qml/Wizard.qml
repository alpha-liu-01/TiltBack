import QtQuick
import QtQuick.Controls

Item {
    id: wizard
    required property var clinic
    property int cornerSize: 72

    signal closed()

    readonly property int btnSize: Math.max(72, Math.round(Math.min(width, height) * 0.09))
    readonly property int chrome: btnSize * 2 + 12

    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_VolumeUp) {
            cycle()
            event.accepted = true
        } else if (event.key === Qt.Key_VolumeDown) {
            looksRight()
            event.accepted = true
        }
    }
    Component.onCompleted: forceActiveFocus()

    function cycle() {
        const step = clinic.wizardStep
        if (step <= 1)
            clinic.cyclePicture()
        else if (step === 2)
            clinic.cycleFinger()
        else if (step === 3)
            clinic.cyclePen()
    }

    function looksRight() {
        const step = clinic.wizardStep
        if (step < 4)
            clinic.setWizardStep(step + 1)
        else
            saveAndLeave()
    }

    function goBack() {
        if (clinic.wizardStep <= 0) {
            leave()
            return
        }
        clinic.setWizardStep(clinic.wizardStep - 1)
    }

    function leave() {
        closed()
        clinic.cancelWizard()
    }

    function saveAndLeave() {
        clinic.finishWizard()
        closed()
    }

    function pickEdge(quarters) {
        clinic.rotatePictureQuarters(quarters)
        clinic.setWizardStep(1)
    }

    readonly property string stepTitle: {
        switch (clinic.wizardStep) {
        case 0: return qsTr("This edge is the top")
        case 1: return qsTr("Look at the picture")
        case 2: return qsTr("Tap the crosshair with a finger")
        case 3: return qsTr("Tap the crosshair with the pen")
        default: return qsTr("Save this home")
        }
    }

    readonly property string stepDetail: {
        switch (clinic.wizardStep) {
        case 0: return qsTr("Tap the edge that should be up, or Looks right if it already is.")
        case 1: return clinic.pictureValue + "\n" + qsTr("Cycle if it is still sideways.")
        case 2: return clinic.fingerValue + "\n" + qsTr("If the mark misses, Cycle residual.")
        case 3: return clinic.penValue + "\n" + qsTr("Independent of finger. Cycle if it misses.")
        default: return clinic.homeLine + "\n" + clinic.persistLine + "\n" + qsTr("Arrow is Phase 6.")
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#1b1b1b"
    }

    Label {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: wizard.chrome + 8
        anchors.leftMargin: wizard.chrome
        anchors.rightMargin: wizard.chrome
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#8ec8ff"
        font.pixelSize: Math.max(22, Math.round(wizard.width * 0.032))
        text: wizard.stepTitle
    }

    Label {
        id: detail
        anchors.top: title.bottom
        anchors.left: title.left
        anchors.right: title.right
        anchors.topMargin: 8
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: "#f2f2f2"
        font.pixelSize: Math.max(18, Math.round(wizard.width * 0.024))
        text: wizard.stepDetail
    }

    Rectangle {
        visible: clinic.wizardStep === 0
        z: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: wizard.chrome
        anchors.rightMargin: wizard.chrome
        height: Math.max(72, wizard.btnSize)
        color: "#2d4a6f"
        Label {
            anchors.centerIn: parent
            text: qsTr("TOP")
            color: "#f2f2f2"
            font.pixelSize: Math.max(20, Math.round(wizard.width * 0.028))
        }
        TapHandler {
            onTapped: wizard.pickEdge(0)
        }
    }
    Rectangle {
        visible: clinic.wizardStep === 0
        z: 1
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: wizard.chrome
        anchors.rightMargin: wizard.chrome
        height: Math.max(72, wizard.btnSize)
        color: "#2d4a6f"
        Label {
            anchors.centerIn: parent
            text: qsTr("BOTTOM")
            color: "#f2f2f2"
            font.pixelSize: Math.max(20, Math.round(wizard.width * 0.028))
        }
        TapHandler {
            onTapped: wizard.pickEdge(2)
        }
    }
    Rectangle {
        visible: clinic.wizardStep === 0
        z: 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: wizard.chrome
        anchors.bottomMargin: wizard.chrome
        width: Math.max(72, wizard.btnSize)
        color: "#2d4a6f"
        Label {
            anchors.centerIn: parent
            rotation: -90
            text: qsTr("LEFT")
            color: "#f2f2f2"
            font.pixelSize: Math.max(20, Math.round(wizard.width * 0.028))
        }
        TapHandler {
            onTapped: wizard.pickEdge(3)
        }
    }
    Rectangle {
        visible: clinic.wizardStep === 0
        z: 1
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: wizard.chrome
        anchors.bottomMargin: wizard.chrome
        width: Math.max(72, wizard.btnSize)
        color: "#2d4a6f"
        Label {
            anchors.centerIn: parent
            rotation: 90
            text: qsTr("RIGHT")
            color: "#f2f2f2"
            font.pixelSize: Math.max(20, Math.round(wizard.width * 0.028))
        }
        TapHandler {
            onTapped: wizard.pickEdge(1)
        }
    }

    Item {
        id: pad
        visible: clinic.wizardStep === 2 || clinic.wizardStep === 3
        z: 1
        anchors.fill: parent
        anchors.margins: wizard.chrome

        property bool tapVisible: false
        property real tapX: width / 2
        property real tapY: height / 2

        onVisibleChanged: tapVisible = false

        Rectangle {
            width: 4
            height: parent.height
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#8ec8ff"
        }
        Rectangle {
            height: 4
            width: parent.width
            anchors.verticalCenter: parent.verticalCenter
            color: "#8ec8ff"
        }
        Rectangle {
            width: 28
            height: 28
            radius: 14
            color: "transparent"
            border.color: "#f2f2f2"
            border.width: 3
            anchors.centerIn: parent
        }

        Rectangle {
            visible: pad.tapVisible
            width: 36
            height: 36
            radius: 18
            color: "#e6ff8e3a"
            border.color: "#ff8e3a"
            border.width: 3
            x: pad.tapX - width / 2
            y: pad.tapY - height / 2
        }

        TapHandler {
            enabled: clinic.wizardStep === 2
            acceptedDevices: PointerDevice.TouchScreen | PointerDevice.Finger
            onTapped: (eventPoint) => {
                pad.tapX = eventPoint.position.x
                pad.tapY = eventPoint.position.y
                pad.tapVisible = true
            }
        }
        TapHandler {
            enabled: clinic.wizardStep === 3
            acceptedDevices: PointerDevice.Stylus
            onTapped: (eventPoint) => {
                pad.tapX = eventPoint.position.x
                pad.tapY = eventPoint.position.y
                pad.tapVisible = true
            }
        }
    }

    Button {
        visible: clinic.wizardStep === 4
        z: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width - 2 * wizard.chrome, 420)
        height: Math.max(72, Math.round(wizard.height * 0.1))
        text: qsTr("Save draft")
        font.pixelSize: Math.max(22, Math.round(wizard.width * 0.028))
        onClicked: wizard.saveAndLeave()
    }

    component CornerPad: Rectangle {
        width: wizard.chrome - 4
        height: wizard.chrome - 4
        z: 200
        color: "#cc111111"
        radius: 12
        border.color: "#8ec8ff"
        border.width: 1

        Grid {
            anchors.fill: parent
            anchors.margins: 4
            columns: 2
            spacing: 4

            Button {
                width: (parent.width - parent.spacing) / 2
                height: (parent.height - parent.spacing) / 2
                text: qsTr("Cycle")
                font.pixelSize: Math.max(14, Math.round(wizard.width * 0.016))
                onClicked: wizard.cycle()
            }
            Button {
                width: (parent.width - parent.spacing) / 2
                height: (parent.height - parent.spacing) / 2
                text: qsTr("Looks right")
                font.pixelSize: Math.max(14, Math.round(wizard.width * 0.016))
                onClicked: wizard.looksRight()
            }
            Button {
                width: (parent.width - parent.spacing) / 2
                height: (parent.height - parent.spacing) / 2
                text: qsTr("Back")
                font.pixelSize: Math.max(14, Math.round(wizard.width * 0.016))
                onClicked: wizard.goBack()
            }
            Button {
                width: (parent.width - parent.spacing) / 2
                height: (parent.height - parent.spacing) / 2
                text: qsTr("Cancel")
                font.pixelSize: Math.max(14, Math.round(wizard.width * 0.016))
                onClicked: wizard.leave()
            }
        }
    }

    CornerPad { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 4 }
    CornerPad { anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4 }
    CornerPad { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 4 }
    CornerPad { anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 4 }
}
