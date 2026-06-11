import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "."
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root

    // Emitted when the user wants to open the settings menu; AwarpMain handles navigation.
    signal openSettingsRequested()

    ColumnLayout {
        objectName: "awarpHomeColumnLayout"

        anchors.fill: parent
        anchors.topMargin: 24 + PageController.safeAreaTopMargin
        anchors.bottomMargin: 24
        anchors.leftMargin: 16
        anchors.rightMargin: 16

        Header1TextType {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter

            horizontalAlignment: Text.AlignHCenter
            text: "AWARP"
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }

        AwarpConnectButton {
            id: connectButton
            objectName: "awarpConnectButton"

            Layout.alignment: Qt.AlignHCenter
        }

        BasicButtonType {
            id: refreshWarpConfigButton
            objectName: "refreshWarpConfigButton"

            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 16
            leftPadding: 16
            rightPadding: 16

            implicitHeight: 36

            defaultColor: AmneziaStyle.color.transparent
            hoveredColor: AmneziaStyle.color.translucentWhite
            pressedColor: AmneziaStyle.color.sheerWhite
            disabledColor: AmneziaStyle.color.mutedGray
            textColor: AmneziaStyle.color.mutedGray
            borderWidth: 0

            buttonTextLabel.lineHeight: 20
            buttonTextLabel.font.pixelSize: 14
            buttonTextLabel.font.weight: 500

            visible: WarpController.hasConfig
            enabled: !WarpController.isBusy

            text: WarpController.isBusy ? qsTr("Обновление конфига...") : qsTr("Обновить конфиг")

            leftImageSource: WarpController.isBusy ? "" : "qrc:/images/controls/refresh-cw.svg"
            leftImageColor: AmneziaStyle.color.mutedGray

            Keys.onEnterPressed: this.clicked()
            Keys.onReturnPressed: this.clicked()

            onClicked: {
                if (ConnectionController.isConnected || ConnectionController.isConnectionInProgress) {
                    PageController.showNotificationMessage(qsTr("Отключитесь от VPN перед обновлением конфига"))
                    return
                }
                WarpController.refreshConfig()
            }

            Connections {
                target: WarpController

                function onConfigUpdated() {
                    PageController.showNotificationMessage(qsTr("Конфиг WARP обновлён"))
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }

        BasicButtonType {
            id: settingsButton
            objectName: "awarpSettingsButton"

            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom

            defaultColor: AmneziaStyle.color.transparent
            hoveredColor: AmneziaStyle.color.translucentWhite
            pressedColor: AmneziaStyle.color.sheerWhite
            disabledColor: AmneziaStyle.color.mutedGray
            textColor: AmneziaStyle.color.paleGray
            borderWidth: 1

            text: qsTr("Настройки")

            leftImageSource: "qrc:/images/controls/settings.svg"

            Keys.onEnterPressed: this.clicked()
            Keys.onReturnPressed: this.clicked()

            onClicked: {
                root.openSettingsRequested()
            }
        }
    }
}
