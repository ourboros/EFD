import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    signal countdownFinished()

    property int remainingSec: 3

    Rectangle {
        anchors.fill: parent
        color: "#1EB18A"

        // 倒數大圓圈
        Rectangle {
            id: pulseCircle
            width: 150
            height: 150
            radius: 75
            color: "#C8F7E3AF"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -30

            SequentialAnimation on scale {
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.15; duration: 500; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 1.15; to: 1.0; duration: 500; easing.type: Easing.InOutQuad }
            }

            Text {
                text: root.remainingSec.toString()
                color: "#1EB18A"
                font.bold: true
                font.pixelSize: 68
                anchors.centerIn: parent
            }
        }

        // 提示文字
        Text {
            text: "請做好準備，即將開始眼動追蹤校準..."
            color: "#FFFFFF"
            font.bold: true
            font.pixelSize: 22
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: pulseCircle.bottom
            anchors.topMargin: 40
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            root.remainingSec -= 1;
            if (root.remainingSec <= 0) {
                running = false;
                root.countdownFinished();
            }
        }
    }
}
