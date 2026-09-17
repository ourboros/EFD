import QtQuick
import QtQuick.Controls

Item {
    id: root
    anchors.fill: parent

    signal instructionFinished()

    Rectangle {
        anchors.fill: parent
        color: "#1EB18A" // Primary Mint Green

        // 頂部大標題
        Text {
            id: titleText
            text: "眼動特徵提取與校準說明"
            color: "#FFFFFF"
            font.bold: true
            font.family: "PingFang TC, Microsoft JhengHei, Noto Sans TC, sans-serif"
            font.pixelSize: 28
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 25
        }

        // 中間內容區域 (左: 演示預覽框, 右: 操作指引卡片)
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -10
            spacing: 30

            // 左側：動態預覽演示框 (Demo Preview Box)
            Rectangle {
                width: 380
                height: 250
                color: "#25291C"
                radius: 8
                border.color: "#96C5F7"
                border.width: 2

                Text {
                    text: "▶ 測試動態路徑演示 (DEMO 預覽)"
                    color: "#F7E3AF"
                    font.bold: true
                    font.pixelSize: 12
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 10
                }

                // 演示巡迴小黃點
                Rectangle {
                    id: demoDot
                    width: 24
                    height: 24
                    radius: 12
                    color: "#F7E3AF"
                    
                    property real animTime: 0.0

                    NumberAnimation on animTime {
                        from: 0.0
                        to: 6.2831853
                        duration: 4000
                        loops: Animation.Infinite
                    }

                    x: (parent.width - width) / 2 + Math.cos(animTime) * (parent.width * 0.35)
                    y: (parent.height - height) / 2 + Math.sin(animTime * 2.0) * (parent.height * 0.28)

                    // 演示點光暈
                    Rectangle {
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        radius: 18
                        color: "transparent"
                        border.color: "#F7E3AF"
                        border.width: 1.5
                        opacity: 0.5
                    }
                }
            }

            // 右側：3 大操作指引卡片
            Column {
                spacing: 12
                width: 340

                Repeater {
                    model: [
                        { title: "1. 臉部正面對齊鏡頭", desc: "保持端正坐姿，確保鏡頭能清晰捕捉完整面部特徵。" },
                        { title: "2. 視線跟隨黃點移動", desc: "測試開始後，請以眼睛專注凝視黃點，並跟隨其移動。" },
                        { title: "3. 保持自然睜眼狀態", desc: "校準過程僅需 5 秒鐘，請保持自然眨眼與視線專注。" }
                    ]

                    delegate: Rectangle {
                        width: 340
                        height: 70
                        radius: 8
                        color: "#B4148C6C"

                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4

                            Text {
                                text: modelData.title
                                color: "#F7E3AF"
                                font.bold: true
                                font.pixelSize: 15
                            }
                            Text {
                                text: modelData.desc
                                color: "#FFFFFF"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }
                        }
                    }
                }
            }
        }

        // 底部「我準備好了，開始校準」按鈕
        Button {
            id: readyBtn
            width: 260
            height: 48
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30

            background: Rectangle {
                radius: 14
                color: readyBtn.down ? "#E0E0E0" : (readyBtn.hovered ? "#F5F5F5" : "#FFFFFF")
            }

            contentItem: Text {
                text: "我準備好了，開始校準"
                color: "#1EB18A"
                font.bold: true
                font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            onClicked: {
                root.instructionFinished();
            }
        }
    }
}
