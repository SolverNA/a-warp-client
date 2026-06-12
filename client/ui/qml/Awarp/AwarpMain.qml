import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import PageEnum 1.0
import Style 1.0

import "."
import "../Config"
import "../Controls2"

Window {
    id: root
    objectName: "mainWindow"

    visible: true
    width: GC.screenWidth
    height: GC.screenHeight
    minimumWidth: GC.isDesktop() ? 360 : 0
    minimumHeight: GC.isDesktop() ? 640 : 0
    maximumWidth: 600
    maximumHeight: 800

    color: AmneziaStyle.color.midnightBlack

    title: "AWARP"

    onClosing: function(close) {
        close.accepted = false
        PageController.closeWindow()
    }

    onSceneGraphError: function(error, message) {
        console.warn("Scene graph error:", error, message)
    }

    // qrc paths to our own pages (no PageEnum)
    readonly property string awarpHomePath: "qrc:/ui/qml/Awarp/AwarpPageHome.qml"
    readonly property string awarpSettingsPath: "qrc:/ui/qml/Awarp/AwarpPageSettings.qml"
    readonly property string awarpWarpConfigPath: "qrc:/ui/qml/Awarp/AwarpPageWarpConfigSettings.qml"
    readonly property string awarpRelaysPath: "qrc:/ui/qml/Awarp/AwarpPageRelays.qml"
    readonly property string awarpAboutPath: "qrc:/ui/qml/Awarp/AwarpPageAbout.qml"

    // Push one of our own pages and wire its navigation signals to this root.
    function pushAwarpPage(pagePath) {
        var item = stackView.push(pagePath, { "objectName": pagePath }, StackView.Immediate)
        if (item) {
            if (item.openSettingsRequested !== undefined) {
                item.openSettingsRequested.connect(root.goToAwarpSettings)
            }
            if (item.openWarpConfigRequested !== undefined) {
                item.openWarpConfigRequested.connect(root.goToWarpConfig)
            }
            if (item.openRelaysRequested !== undefined) {
                item.openRelaysRequested.connect(root.goToAwarpRelays)
            }
            if (item.openAboutRequested !== undefined) {
                item.openAboutRequested.connect(root.goToAwarpAbout)
            }
        }
        return item
    }

    function goToAwarpHome() {
        while (stackView.depth > 1) {
            stackView.pop(StackView.Immediate)
        }
    }

    function goToAwarpSettings() {
        pushAwarpPage(awarpSettingsPath)
    }

    function goToWarpConfig() {
        pushAwarpPage(awarpWarpConfigPath)
    }

    function goToAwarpRelays() {
        pushAwarpPage(awarpRelaysPath)
    }

    function goToAwarpAbout() {
        pushAwarpPage(awarpAboutPath)
    }

    Item { // focus handling — objectName required by focusController
        id: defaultFocusItem
        objectName: "defaultFocusItem"

        focus: true

        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Tab:
            case Qt.Key_Down:
            case Qt.Key_Right:
                FocusController.nextKeyTabItem()
                break
            case Qt.Key_Backtab:
            case Qt.Key_Up:
            case Qt.Key_Left:
                FocusController.previousKeyTabItem()
                break
            default:
                PageController.keyPressEvent(event.key)
                event.accepted = true
            }
        }
    }

    StackViewType {
        id: stackView
        objectName: "awarpStackView"

        anchors.fill: parent

        Component.onCompleted: {
            root.pushAwarpPage(root.awarpHomePath)

            ServersUiController.setProcessedServerId(ServersUiController.defaultServerId)

            // WARP-only client: auto-request the config on first launch
            if (!WarpController.hasConfig) {
                WarpController.fetchNewConfig()
            }
        }

        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Tab:
            case Qt.Key_Down:
            case Qt.Key_Right:
                FocusController.nextKeyTabItem()
                break
            case Qt.Key_Backtab:
            case Qt.Key_Up:
            case Qt.Key_Left:
                FocusController.previousKeyTabItem()
                break
            default:
                PageController.keyPressEvent(event.key)
                event.accepted = true
            }
        }
    }

    // Bridge to the authored PageController so reusable settings pages keep working.
    Connections {
        objectName: "pageControllerConnections"

        target: PageController

        function onRaiseMainWindow() {
            root.show()
            root.raise()
            root.requestActivate()
        }

        function onHideMainWindow() {
            root.hide()
        }

        function onShowErrorMessage(errorMessage) {
            popupErrorMessage.text = errorMessage
            popupErrorMessage.open()
        }

        function onShowNotificationMessage(message) {
            popupNotificationMessage.text = message
            popupNotificationMessage.closeButtonVisible = false
            popupNotificationMessage.open()
            popupNotificationTimer.start()
        }

        function onShowBusyIndicator(visible) {
            busyIndicator.visible = visible
            PageController.disableControls(visible)
        }

        // Authored under-pages navigate via getPagePath through these signals.
        function onGoToPage(page, slide) {
            var pagePath = PageController.getPagePath(page)
            if (slide) {
                stackView.push(pagePath, { "objectName": pagePath }, StackView.PushTransition)
            } else {
                stackView.push(pagePath, { "objectName": pagePath }, StackView.Immediate)
            }
        }

        function onClosePage() {
            if (stackView.depth <= 1) {
                PageController.hideWindow()
                return
            }
            stackView.pop()
        }

        function onGoToPageHome() {
            root.goToAwarpHome()
        }

        function onGoToPageSettings() {
            root.goToAwarpHome()
            root.goToAwarpSettings()
        }

        function onGoToStartPage() {
            root.goToAwarpHome()
        }

        function onEscapePressed() {
            PageController.closePage()
        }

        function onDisableControls(disabled) {
            stackView.enabled = !disabled
        }
    }

    Connections {
        objectName: "settingsControllerConnections"

        target: SettingsController

        function onChangeSettingsFinished(finishedMessage) {
            PageController.showNotificationMessage(finishedMessage)
        }
    }

    // WARP config lifecycle notifications (moved here from authored PageStart).
    Connections {
        objectName: "warpControllerConnections"

        target: WarpController

        function onConfigReady() {
            PageController.showNotificationMessage(qsTr("Конфиг WARP получен"))
        }

        function onConfigUpdated() {
            PageController.showNotificationMessage(qsTr("Конфиг WARP обновлён"))
        }

        function onErrorOccurred(error) {
            PageController.showErrorMessage(error)
        }
    }

    Item {
        objectName: "popupNotificationItem"

        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom

        implicitHeight: popupNotificationMessage.height

        PopupType {
            id: popupNotificationMessage
        }

        Timer {
            id: popupNotificationTimer

            interval: 3000
            repeat: false
            running: false
            onTriggered: {
                popupNotificationMessage.close()
            }
        }
    }

    Item {
        objectName: "popupErrorMessageItem"

        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom

        implicitHeight: popupErrorMessage.height

        PopupType {
            id: popupErrorMessage
        }
    }

    Item {
        objectName: "busyIndicatorItem"

        anchors.fill: parent

        BusyIndicatorType {
            id: busyIndicator
            anchors.centerIn: parent
            z: 1
        }
    }

    FileDialog {
        id: mainFileDialog
        objectName: "mainFileDialog"

        property bool isSaveMode: false

        fileMode: isSaveMode ? FileDialog.SaveFile : FileDialog.OpenFile

        onAccepted: SystemController.fileDialogClosed(true)
        onRejected: SystemController.fileDialogClosed(false)
    }
}
