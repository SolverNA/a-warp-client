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

    ListViewType {
        id: listView

        anchors.fill: parent

        header: ColumnLayout {
            width: listView.width

            BackButtonType {
                id: backButton

                Layout.topMargin: 20 + PageController.safeAreaTopMargin
                Layout.fillWidth: true
            }

            Image {
                id: image
                source: "qrc:/images/amneziaBigLogo.png"

                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.preferredWidth: 291
                Layout.preferredHeight: 224
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("О приложении AWARP")
                horizontalAlignment: Text.AlignHCenter
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                horizontalAlignment: Text.AlignHCenter

                font.pixelSize: 14

                text: qsTr("AWARP — клиент для подключения к WARP.")
                color: AmneziaStyle.color.paleGray
            }
        }

        model: contacts

        delegate: ColumnLayout {
            width: listView.width

            spacing: 0

            LabelWithButtonType {
                Layout.fillWidth: true

                text: title
                descriptionText: description
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                leftImageSource: imageSource

                clickedFunction: handler
            }

            DividerType {}
        }
    }

    property list<QtObject> contacts: [
        website
    ]

    QtObject {
        id: website

        readonly property string title: qsTr("Веб-сайт")
        readonly property string description: qsTr("Посетить официальный сайт")
        readonly property string imageSource: "qrc:/images/controls/amnezia.svg"
        readonly property var handler: function() {
            Qt.openUrlExternally("https://awarp.app/")
        }
    }
}
