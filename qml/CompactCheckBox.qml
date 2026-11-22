import QtQuick
import QtQuick.Controls

CheckBox {
    id:control
    implicitHeight:28
    implicitWidth:contentItem.implicitWidth+leftPadding+rightPadding
    leftPadding:0;rightPadding:4;topPadding:4;bottomPadding:4;spacing:7
    indicator:Rectangle {
        x:control.leftPadding;y:control.topPadding+(control.availableHeight-height)/2
        width:16;height:16;radius:3
        color:control.checked?appTheme.colors.accent:appTheme.backgrounds[control.hovered?"controlHover":"panel"]
        border.color:control.activeFocus?appTheme.colors.focusStrong:control.checked?appTheme.colors.accent:appTheme.colors.borderStrong
        Text{anchors.centerIn:parent;text:control.checked?"✓":"";color:appTheme.colors.accentText;font.pixelSize:12}
    }
    contentItem:Text {
        text:control.text;font:control.font;color:control.enabled?appTheme.colors.text:appTheme.colors.disabled
        leftPadding:control.indicator.width+control.spacing
        verticalAlignment:Text.AlignVCenter
    }
}
