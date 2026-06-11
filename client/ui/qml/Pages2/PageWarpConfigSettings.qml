import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + PageController.safeAreaTopMargin

        onFocusChanged: {
            if (this.activeFocus) {
                listView.positionViewAtBeginning()
            }
        }
    }

    SmartScroll {
        id: smartScroll
        listView: listView
    }

    ListViewType {
        id: listView

        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: parent.left

        header: ColumnLayout {
            width: listView.width
            spacing: 16

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Настройки конфига")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: !WarpController.hasConfig

                color: AmneziaStyle.color.mutedGray
                text: qsTr("Конфиг WARP ещё не получен — показаны значения по умолчанию. Получите конфиг на главном экране, чтобы изменять настройки.")
            }
        }

        model: 1 // fake model to force the ListView to be created without a model

        delegate: ColumnLayout {
            id: delegateItem

            width: listView.width
            spacing: 0

            enabled: WarpController.hasConfig

            function loadFields(fields) {
                if (!fields) {
                    return
                }
                if (fields.junkPacketCount !== undefined) junkPacketCountTextField.textField.text = fields.junkPacketCount
                if (fields.junkPacketMinSize !== undefined) junkPacketMinSizeTextField.textField.text = fields.junkPacketMinSize
                if (fields.junkPacketMaxSize !== undefined) junkPacketMaxSizeTextField.textField.text = fields.junkPacketMaxSize
                if (fields.initPacketJunkSize !== undefined) initPacketJunkSizeTextField.textField.text = fields.initPacketJunkSize
                if (fields.responsePacketJunkSize !== undefined) responsePacketJunkSizeTextField.textField.text = fields.responsePacketJunkSize
                if (fields.initPacketMagicHeader !== undefined) initPacketMagicHeaderTextField.textField.text = fields.initPacketMagicHeader
                if (fields.responsePacketMagicHeader !== undefined) responsePacketMagicHeaderTextField.textField.text = fields.responsePacketMagicHeader
                if (fields.underloadPacketMagicHeader !== undefined) underloadPacketMagicHeaderTextField.textField.text = fields.underloadPacketMagicHeader
                if (fields.transportPacketMagicHeader !== undefined) transportPacketMagicHeaderTextField.textField.text = fields.transportPacketMagicHeader
                if (fields.specialJunk1 !== undefined) specialJunk1TextArea.textAreaText = fields.specialJunk1
                if (fields.mtu !== undefined) mtuTextField.textField.text = fields.mtu
                if (fields.dns !== undefined) dnsTextField.textField.text = fields.dns
                if (fields.endpointHost !== undefined) endpointHostTextField.textField.text = fields.endpointHost
                if (fields.endpointPort !== undefined) endpointPortTextField.textField.text = fields.endpointPort
                if (fields.allowedIps !== undefined) allowedIpsTextField.textField.text = fields.allowedIps
                // Read-only session fields are present only in the saved config
                clientIpV4TextField.textField.text = fields.clientIpV4 !== undefined ? fields.clientIpV4 : ""
                clientIpV6TextField.textField.text = fields.clientIpV6 !== undefined ? fields.clientIpV6 : ""
                peerPublicKeyTextField.textField.text = fields.peerPublicKey !== undefined ? fields.peerPublicKey : ""
            }

            function reload() {
                if (WarpController.hasConfig) {
                    loadFields(WarpController.getConfigFields())
                } else {
                    loadFields(WarpController.getDefaultConfigFields())
                }
            }

            function collectFields() {
                return {
                    "junkPacketCount": junkPacketCountTextField.textField.text.trim(),
                    "junkPacketMinSize": junkPacketMinSizeTextField.textField.text.trim(),
                    "junkPacketMaxSize": junkPacketMaxSizeTextField.textField.text.trim(),
                    "initPacketJunkSize": initPacketJunkSizeTextField.textField.text.trim(),
                    "responsePacketJunkSize": responsePacketJunkSizeTextField.textField.text.trim(),
                    "initPacketMagicHeader": initPacketMagicHeaderTextField.textField.text.trim(),
                    "responsePacketMagicHeader": responsePacketMagicHeaderTextField.textField.text.trim(),
                    "underloadPacketMagicHeader": underloadPacketMagicHeaderTextField.textField.text.trim(),
                    "transportPacketMagicHeader": transportPacketMagicHeaderTextField.textField.text.trim(),
                    "specialJunk1": specialJunk1TextArea.textAreaText.trim(),
                    "mtu": mtuTextField.textField.text.trim(),
                    "dns": dnsTextField.textField.text.trim(),
                    "endpointHost": endpointHostTextField.textField.text.trim(),
                    "endpointPort": endpointPortTextField.textField.text.trim(),
                    "allowedIps": allowedIpsTextField.textField.text.trim()
                }
            }

            function validateFields() {
                var jc = parseInt(junkPacketCountTextField.textField.text)
                var jmin = parseInt(junkPacketMinSizeTextField.textField.text)
                var jmax = parseInt(junkPacketMaxSizeTextField.textField.text)
                if (isNaN(jc) || jc < 0) {
                    return qsTr("Jc должен быть неотрицательным числом")
                }
                if (isNaN(jmin) || isNaN(jmax) || jmin < 0 || jmax < 0) {
                    return qsTr("Jmin и Jmax должны быть неотрицательными числами")
                }
                if (jmin > jmax) {
                    return qsTr("Jmin не может быть больше Jmax")
                }

                var s1 = parseInt(initPacketJunkSizeTextField.textField.text)
                var s2 = parseInt(responsePacketJunkSizeTextField.textField.text)
                if (isNaN(s1) || s1 < 0 || isNaN(s2) || s2 < 0) {
                    return qsTr("S1 и S2 должны быть неотрицательными числами")
                }

                var headers = [
                    initPacketMagicHeaderTextField.textField.text.trim(),
                    responsePacketMagicHeaderTextField.textField.text.trim(),
                    underloadPacketMagicHeaderTextField.textField.text.trim(),
                    transportPacketMagicHeaderTextField.textField.text.trim()
                ]
                for (var i = 0; i < headers.length; i++) {
                    if (headers[i] === "") {
                        return qsTr("H1–H4 не могут быть пустыми")
                    }
                    for (var j = i + 1; j < headers.length; j++) {
                        if (headers[i] === headers[j]) {
                            return qsTr("Значения H1–H4 должны быть различными")
                        }
                    }
                }

                var mtu = parseInt(mtuTextField.textField.text)
                if (isNaN(mtu) || mtu < 576 || mtu > 65535) {
                    return qsTr("MTU должен быть в диапазоне 576–65535")
                }

                if (endpointHostTextField.textField.text.trim() === "") {
                    return qsTr("Endpoint (адрес) не может быть пустым")
                }
                var port = parseInt(endpointPortTextField.textField.text)
                if (isNaN(port) || port < 1 || port > 65535) {
                    return qsTr("Порт должен быть в диапазоне 1–65535")
                }

                if (allowedIpsTextField.textField.text.trim() === "") {
                    return qsTr("AllowedIPs не может быть пустым")
                }

                return ""
            }

            Component.onCompleted: {
                reload()
            }

            Connections {
                target: WarpController

                function onConfigReady() {
                    delegateItem.reload()
                }

                function onConfigUpdated() {
                    delegateItem.reload()
                }
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Обфускация AmneziaWG")
            }

            AwgTextField {
                id: junkPacketCountTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Jc — количество junk-пакетов")
                textField.validator: IntValidator { bottom: 0; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(junkPacketCountTextField)
                    }
                }
            }

            AwgTextField {
                id: junkPacketMinSizeTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Jmin — минимальный размер junk-пакета")
                textField.validator: IntValidator { bottom: 0; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(junkPacketMinSizeTextField)
                    }
                }
            }

            AwgTextField {
                id: junkPacketMaxSizeTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Jmax — максимальный размер junk-пакета")
                textField.validator: IntValidator { bottom: 0; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(junkPacketMaxSizeTextField)
                    }
                }
            }

            AwgTextField {
                id: initPacketJunkSizeTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("S1 — размер junk в init-пакете")
                textField.validator: IntValidator { bottom: 0; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(initPacketJunkSizeTextField)
                    }
                }
            }

            AwgTextField {
                id: responsePacketJunkSizeTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("S2 — размер junk в response-пакете")
                textField.validator: IntValidator { bottom: 0; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(responsePacketJunkSizeTextField)
                    }
                }
            }

            AwgTextField {
                id: initPacketMagicHeaderTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("H1 — magic header init-пакета")
                textField.validator: RegularExpressionValidator {
                    regularExpression: /^(\d+)(-\d+)?$/
                }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(initPacketMagicHeaderTextField)
                    }
                }
            }

            AwgTextField {
                id: responsePacketMagicHeaderTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("H2 — magic header response-пакета")
                textField.validator: RegularExpressionValidator {
                    regularExpression: /^(\d+)(-\d+)?$/
                }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(responsePacketMagicHeaderTextField)
                    }
                }
            }

            AwgTextField {
                id: underloadPacketMagicHeaderTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("H3 — magic header underload-пакета")
                textField.validator: RegularExpressionValidator {
                    regularExpression: /^(\d+)(-\d+)?$/
                }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(underloadPacketMagicHeaderTextField)
                    }
                }
            }

            AwgTextField {
                id: transportPacketMagicHeaderTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("H4 — magic header transport-пакета")
                textField.validator: RegularExpressionValidator {
                    regularExpression: /^(\d+)(-\d+)?$/
                }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(transportPacketMagicHeaderTextField)
                    }
                }
            }

            LabelTextType {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                color: AmneziaStyle.color.mutedGray
                text: qsTr("I1 — специальный junk-пакет")
            }

            TextAreaType {
                id: specialJunk1TextArea

                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.preferredHeight: 200
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Сеть")
            }

            AwgTextField {
                id: mtuTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("MTU")
                textField.validator: IntValidator { bottom: 576; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(mtuTextField)
                    }
                }
            }

            AwgTextField {
                id: dnsTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                checkEmptyText: false

                headerText: qsTr("DNS (через запятую)")

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(dnsTextField)
                    }
                }
            }

            AwgTextField {
                id: endpointHostTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Endpoint — адрес сервера")

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(endpointHostTextField)
                    }
                }
            }

            AwgTextField {
                id: endpointPortTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Endpoint — порт")
                textField.maximumLength: 5
                textField.validator: IntValidator { bottom: 1; top: 65535 }

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(endpointPortTextField)
                    }
                }
            }

            AwgTextField {
                id: allowedIpsTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("AllowedIPs (через запятую)")

                textField.onActiveFocusChanged: {
                    if (textField.activeFocus) {
                        smartScroll.scrollToItem(allowedIpsTextField)
                    }
                }
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Текущая сессия (только просмотр)")
            }

            AwgTextField {
                id: clientIpV4TextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                enabled: false
                checkEmptyText: false

                headerText: qsTr("IP клиента (IPv4)")
            }

            AwgTextField {
                id: clientIpV6TextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                enabled: false
                checkEmptyText: false

                headerText: qsTr("IP клиента (IPv6)")
            }

            AwgTextField {
                id: peerPublicKeyTextField

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                enabled: false
                checkEmptyText: false

                headerText: qsTr("Публичный ключ пира")
            }

            BasicButtonType {
                id: resetButton

                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.paleGray
                borderWidth: 1

                text: qsTr("Сбросить к значениям по умолчанию")

                clickedFunc: function() {
                    delegateItem.loadFields(WarpController.getDefaultConfigFields())
                    PageController.showNotificationMessage(qsTr("Значения по умолчанию подставлены — нажмите «Сохранить», чтобы применить"))
                }
            }

            BasicButtonType {
                id: saveButton

                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.bottomMargin: 24
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Сохранить")

                clickedFunc: function() {
                    if (ConnectionController.isConnected || ConnectionController.isConnectionInProgress) {
                        PageController.showNotificationMessage(qsTr("Отключитесь от VPN перед изменением настроек"))
                        return
                    }

                    var error = delegateItem.validateFields()
                    if (error !== "") {
                        PageController.showNotificationMessage(error)
                        return
                    }

                    if (WarpController.saveConfig(delegateItem.collectFields())) {
                        PageController.showNotificationMessage(qsTr("Настройки конфига сохранены"))
                    }
                }
            }
        }
    }
}
