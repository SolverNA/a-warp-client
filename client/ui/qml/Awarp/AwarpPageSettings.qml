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

    // Our own WARP config page has no PageEnum; AwarpMain handles it via this signal.
    signal openWarpConfigRequested()

    // Our own Relays page has no PageEnum; AwarpMain handles it via this signal.
    signal openRelaysRequested()

    // Our own About page has no PageEnum; AwarpMain handles it via this signal.
    signal openAboutRequested()

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

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 16
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Настройки")
            }
        }

        model: settingsEntries

        delegate: ColumnLayout {
            width: listView.width

            spacing: 0

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: isVisible

                text: title
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                leftImageSource: leftImagePath

                clickedFunction: clickedHandler
            }

            DividerType {
                visible: isVisible
            }
        }

        footer: ColumnLayout {
            width: listView.width

            LabelWithButtonType {
                id: close

                visible: GC.isDesktop()
                Layout.fillWidth: true

                text: qsTr("Закрыть приложение")
                leftImageSource: "qrc:/images/controls/x-circle.svg"
                isLeftImageHoverEnabled: false

                clickedFunction: function() {
                    PageController.closeApplication()
                }
            }

            DividerType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: GC.isDesktop()
            }
        }
    }

    property list<QtObject> settingsEntries: [
        warpConfig,
        relays,
        connection,
        dns,
        killSwitch,
        splitTunneling,
        logging,
        about
    ]

    QtObject {
        id: warpConfig

        property string title: qsTr("Настройки конфига")
        readonly property string leftImagePath: "qrc:/images/controls/file-cog-2.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            root.openWarpConfigRequested()
        }
    }

    QtObject {
        id: relays

        property string title: qsTr("Релеи")
        readonly property string leftImagePath: "qrc:/images/controls/globe-2.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            root.openRelaysRequested()
        }
    }

    QtObject {
        id: connection

        property string title: qsTr("Соединение")
        readonly property string leftImagePath: "qrc:/images/controls/radio.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            PageController.goToPage(PageEnum.PageSettingsConnection)
        }
    }

    QtObject {
        id: dns

        property string title: qsTr("DNS")
        readonly property string leftImagePath: "qrc:/images/controls/globe-2.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            PageController.goToPage(PageEnum.PageSettingsDns)
        }
    }

    QtObject {
        id: killSwitch

        property string title: qsTr("Kill Switch")
        readonly property string leftImagePath: "qrc:/images/controls/app.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            PageController.goToPage(PageEnum.PageSettingsKillSwitch)
        }
    }

    QtObject {
        id: splitTunneling

        property string title: qsTr("Раздельное туннелирование")
        readonly property string leftImagePath: "qrc:/images/controls/split-tunneling.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
        }
    }

    QtObject {
        id: logging

        property string title: qsTr("Логи")
        readonly property string leftImagePath: "qrc:/images/controls/bug.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            PageController.goToPage(PageEnum.PageSettingsLogging)
        }
    }

    QtObject {
        id: about

        property string title: qsTr("О приложении")
        readonly property string leftImagePath: "qrc:/images/controls/info.svg"
        property bool isVisible: true
        readonly property var clickedHandler: function() {
            root.openAboutRequested()
        }
    }
}
