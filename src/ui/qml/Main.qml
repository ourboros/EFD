import QtQuick
import QtQuick.Window
import QtQuick.Controls

Window {
    id: mainWindow
    width: 960
    height: 640
    visible: true
    title: "Eye Fatigue Detection (EFD) - 視覺校準與疲勞監控系統"

    StackView {
        id: mainStack
        anchors.fill: parent
        initialItem: welcomeView
    }

    // 階段 1：系統歡迎介面
    Component {
        id: welcomeView
        WelcomeScreen {
            onStartClicked: {
                mainStack.replace(guideView);
            }
        }
    }

    // 階段 2：眼動數據提取說明介面 (中央黃點)
    Component {
        id: guideView
        CalibrationGuide {
            onGuideFinished: {
                mainStack.replace(activeCalibView);
            }
        }
    }

    // 階段 3：眼動數據動態提取介面 (移動黃點)
    Component {
        id: activeCalibView
        ActiveCalibrationView {
            onCalibrationCompleted: {
                mainStack.replace(dashboardView);
            }
        }
    }

    // 階段 4：即時疲勞監控儀表板
    Component {
        id: dashboardView
        Rectangle {
            anchors.fill: parent
            color: "#25291C" // EFD Dark Background

            Column {
                anchors.centerIn: parent
                spacing: 24
                width: Math.min(parent.width * 0.9, 700)

                Text {
                    text: "EFD 眼睛疲勞即時監控中心"
                    color: "#FFFFFF"
                    font.pixelSize: 26
                    font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Rectangle {
                    width: parent.width
                    height: 120
                    radius: 12
                    color: "#1EB18A"
                    anchors.horizontalCenter: parent.horizontalCenter

                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            text: "生理狀態：正常清醒 (Relaxed)"
                            color: "#FFFFFF"
                            font.pixelSize: 22
                            font.bold: true
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: "五執行緒管線正常分析中 (30 FPS)"
                            color: "#FFFFFF"
                            font.pixelSize: 15
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }

                Button {
                    text: "重新校準基準"
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: mainStack.replace(guideView)
                }
            }
        }
    }
}

