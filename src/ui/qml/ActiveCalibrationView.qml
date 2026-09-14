import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    signal calibrationCompleted()

    // 滿版薄荷綠背景 (#1EB18A)
    Rectangle {
        anchors.fill: parent
        color: "#1EB18A"

        // 動態移動的暖黃色圓點 (#F7E3AF)
        Rectangle {
            id: movingDot
            width: 48
            height: 48
            radius: 24
            color: "#F7E3AF"
            x: root.width * 0.5 - 24
            y: root.height * 0.5 - 24

            SequentialAnimation {
                id: dotMovement
                running: true

                // 1. 移動至左上角 (圖二位置)
                ParallelAnimation {
                    NumberAnimation { target: movingDot; property: "x"; to: root.width * 0.12 - 24; duration: 1000; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: movingDot; property: "y"; to: root.height * 0.15 - 24; duration: 1000; easing.type: Easing.InOutQuad }
                }
                PauseAnimation { duration: 500 }

                // 2. 移動至右上角 (圖二位置)
                ParallelAnimation {
                    NumberAnimation { target: movingDot; property: "x"; to: root.width * 0.88 - 24; duration: 1200; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: movingDot; property: "y"; to: root.height * 0.15 - 24; duration: 1200; easing.type: Easing.InOutQuad }
                }
                PauseAnimation { duration: 500 }

                // 3. 移動至右下角
                ParallelAnimation {
                    NumberAnimation { target: movingDot; property: "x"; to: root.width * 0.88 - 24; duration: 1000; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: movingDot; property: "y"; to: root.height * 0.80 - 24; duration: 1000; easing.type: Easing.InOutQuad }
                }
                PauseAnimation { duration: 500 }

                // 4. 移動至左下角
                ParallelAnimation {
                    NumberAnimation { target: movingDot; property: "x"; to: root.width * 0.12 - 24; duration: 1200; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: movingDot; property: "y"; to: root.height * 0.80 - 24; duration: 1200; easing.type: Easing.InOutQuad }
                }
                PauseAnimation { duration: 500 }

                // 5. 回歸中心
                ParallelAnimation {
                    NumberAnimation { target: movingDot; property: "x"; to: root.width * 0.50 - 24; duration: 800; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: movingDot; property: "y"; to: root.height * 0.50 - 24; duration: 800; easing.type: Easing.InOutQuad }
                }

                onFinished: {
                    root.calibrationCompleted();
                }
            }
        }

        // 底部提示文字
        Text {
            id: statusText
            text: "眼動特徵多角度提取中..."
            color: "#FFFFFF"
            font.bold: true
            font.pixelSize: 20
            font.family: "PingFang TC, Microsoft JhengHei, Noto Sans TC, sans-serif"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 50
        }
    }
}

