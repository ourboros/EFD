import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    signal guideFinished()

    // 滿版薄荷綠背景 (#1EB18A)
    Rectangle {
        anchors.fill: parent
        color: "#1EB18A"

        // 畫面中央暖黃色圓點 (#F7E3AF)
        Rectangle {
            id: centerDot
            width: 48
            height: 48
            radius: 24
            color: "#F7E3AF"
            anchors.centerIn: parent

            // 脈衝光暈動畫
            SequentialAnimation on scale {
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.25; duration: 600; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 1.25; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
            }
        }

        // 底部說明文字: "請凝視畫面上的黃點並跟隨他移動"
        Text {
            id: guideText
            text: "請凝視畫面上的黃點並跟隨他移動"
            color: "#FFFFFF"
            font.bold: true
            font.family: "PingFang TC, Microsoft JhengHei, Noto Sans TC, sans-serif"
            font.pixelSize: Math.max(22, Math.min(root.width * 0.035, 36))
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 60
        }
    }

    // 2.5 秒後自動進入動態多點校準
    Timer {
        interval: 2500
        running: true
        repeat: false
        onTriggered: {
            root.guideFinished();
        }
    }
}

