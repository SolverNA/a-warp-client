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

    // Editor form state. editId == "" means "add new"; otherwise editing that id.
    property bool formVisible: false
    property string editId: ""

    function openAddForm() {
        editId = ""
        labelField.textField.text = ""
        hostField.textField.text = ""
        portField.textField.text = ""
        countryField.textField.text = ""
        formVisible = true
    }

    function openEditForm(id, label, host, port, country) {
        editId = id
        labelField.textField.text = label
        hostField.textField.text = host
        portField.textField.text = port > 0 ? String(port) : ""
        countryField.textField.text = country
        formVisible = true
    }

    function submitForm() {
        var label = labelField.textField.text
        var host = hostField.textField.text
        var port = parseInt(portField.textField.text)
        var country = countryField.textField.text
        if (!host || isNaN(port) || port <= 0 || port > 65535) {
            PageController.showErrorMessage(qsTr("Укажите корректный host и port"))
            return
        }
        var ok
        if (editId === "") {
            ok = RelaysController.addRelay(label, host, port, country)
        } else {
            ok = RelaysController.editRelay(editId, label, host, port, country)
        }
        if (ok) {
            formVisible = false
        } else {
            PageController.showErrorMessage(qsTr("Не удалось сохранить релей"))
        }
    }

    Component.onCompleted: {
        // One-shot ping pass when the page opens (no recurring timer).
        RelaysController.pingAll()
    }

    ListViewType {
        id: listView

        anchors.fill: parent
        visible: !root.formVisible

        header: ColumnLayout {
            width: listView.width

            BackButtonType {
                id: backButton

                Layout.topMargin: 20 + PageController.safeAreaTopMargin
                Layout.fillWidth: true
            }

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 16
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Релеи")
            }

            // Auto (Cloudflare) entry — returns the endpoint to automatic mode.
            LabelWithButtonType {
                Layout.fillWidth: true

                text: qsTr("Авто (Cloudflare)")
                descriptionText: WarpController.endpointAuto ? qsTr("Выбрано") : ""
                leftImageSource: "qrc:/images/controls/refresh-cw.svg"
                rightImageSource: WarpController.endpointAuto
                                  ? "qrc:/images/controls/check.svg" : ""

                clickedFunction: function() {
                    RelaysController.selectAuto()
                }
            }

            DividerType {}
        }

        model: RelaysController

        delegate: ColumnLayout {
            width: listView.width

            spacing: 0

            LabelWithButtonType {
                Layout.fillWidth: true

                text: label
                descriptionText: {
                    var base = host + ":" + port
                    if (latencyMs >= 0) {
                        base += "  •  " + latencyMs + " " + qsTr("мс")
                    }
                    return base
                }

                leftImageSource: country && country.length === 2
                                 ? "qrc:/countriesFlags/images/flagKit/" + country.toUpperCase() + ".svg"
                                 : "qrc:/images/controls/globe-2.svg"
                isLeftImageHoverEnabled: false

                rightImageSource: isSelected && !WarpController.endpointAuto
                                  ? "qrc:/images/controls/check.svg" : ""

                clickedFunction: function() {
                    RelaysController.selectRelay(id)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                spacing: 8

                BasicButtonType {
                    Layout.fillWidth: true
                    implicitHeight: 36

                    defaultColor: AmneziaStyle.color.transparent
                    hoveredColor: AmneziaStyle.color.translucentWhite
                    pressedColor: AmneziaStyle.color.sheerWhite
                    textColor: AmneziaStyle.color.paleGray
                    borderWidth: 1

                    text: qsTr("Пинг")

                    clickedFunc: function() {
                        RelaysController.pingRelay(id)
                    }
                }

                BasicButtonType {
                    Layout.fillWidth: true
                    implicitHeight: 36

                    visible: !isBuiltin

                    defaultColor: AmneziaStyle.color.transparent
                    hoveredColor: AmneziaStyle.color.translucentWhite
                    pressedColor: AmneziaStyle.color.sheerWhite
                    textColor: AmneziaStyle.color.paleGray
                    borderWidth: 1

                    text: qsTr("Изменить")

                    clickedFunc: function() {
                        root.openEditForm(id, label, host, port, country)
                    }
                }

                BasicButtonType {
                    Layout.fillWidth: true
                    implicitHeight: 36

                    visible: !isBuiltin

                    defaultColor: AmneziaStyle.color.transparent
                    hoveredColor: AmneziaStyle.color.translucentWhite
                    pressedColor: AmneziaStyle.color.sheerWhite
                    textColor: AmneziaStyle.color.vibrantRed
                    borderWidth: 1

                    text: qsTr("Удалить")

                    clickedFunc: function() {
                        RelaysController.removeRelay(id)
                    }
                }
            }

            DividerType {}
        }

        footer: ColumnLayout {
            width: listView.width

            BasicButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Обновить пинг")

                clickedFunc: function() {
                    RelaysController.pingAll()
                }
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                textColor: AmneziaStyle.color.paleGray
                borderWidth: 1

                text: qsTr("Добавить релей")

                clickedFunc: function() {
                    root.openAddForm()
                }
            }
        }
    }

    // Add/edit form overlay
    Flickable {
        anchors.fill: parent
        visible: root.formVisible
        contentHeight: formColumn.implicitHeight
        clip: true

        ColumnLayout {
            id: formColumn
            width: parent.width
            spacing: 16

            BackButtonType {
                Layout.topMargin: 20 + PageController.safeAreaTopMargin
                Layout.fillWidth: true

                backButtonFunction: function() {
                    root.formVisible = false
                }
            }

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: root.editId === "" ? qsTr("Новый релей") : qsTr("Изменить релей")
            }

            TextFieldWithHeaderType {
                id: labelField
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                headerText: qsTr("Название")
            }

            TextFieldWithHeaderType {
                id: hostField
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                headerText: qsTr("Host (IP или домен)")
            }

            TextFieldWithHeaderType {
                id: portField
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                headerText: qsTr("Port")
            }

            TextFieldWithHeaderType {
                id: countryField
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                headerText: qsTr("Страна (ISO2, напр. RU)")
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                text: qsTr("Сохранить")

                clickedFunc: function() {
                    root.submitForm()
                }
            }
        }
    }
}
