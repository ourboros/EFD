import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    signal startClicked()

    // 滿版薄荷綠背景 (#1EB18A)
    Rectangle {
        id: background
        anchors.fill: parent
        color: "#1EB18A"

        Column {
            anchors.centerIn: parent
            spacing: 28
            width: Math.min(parent.width * 0.9, 800)

            // Logo 圖像 (design/1x/資產 7.png) - 縮小 50%
            Image {
                id: logoImage
                source: "assets/logo.png"
                width: 110
                height: 66
                fillMode: Image.PreserveAspectFit
                anchors.horizontalCenter: parent.horizontalCenter
                smooth: true
                asynchronous: true
            }

            // 標題文字: "感謝協助測試EFD"
            Text {
                id: titleText
                text: "感謝協助測試EFD"
                color: "#FFFFFF"
                font.bold: true
                font.family: "PingFang TC, Microsoft JhengHei, Noto Sans TC, sans-serif"
                font.pixelSize: Math.max(28, Math.min(root.width * 0.05, 48))
                font.letterSpacing: 1.0
                horizontalAlignment: Text.AlignHCenter
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // 開始按鈕: "開始進入系統"
            Rectangle {
                id: startBtn
                width: 200
                height: 52
                radius: 12
                color: "#FFFFFF"
                anchors.horizontalCenter: parent.horizontalCenter

                scale: btnMouse.containsPress ? 0.96 : (btnMouse.containsMouse ? 1.04 : 1.0)
                Behavior on scale {
                    NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
                }

                Text {
                    anchors.centerIn: parent
                    text: "開始進入系統"
                    color: "#1EB18A"
                    font.pixelSize: 18
                    font.bold: true
                    font.family: "PingFang TC, Microsoft JhengHei, Noto Sans TC, sans-serif"
                }

                MouseArea {
                    id: btnMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.startClicked();
                    }
                }
            }
        }
    }
}

