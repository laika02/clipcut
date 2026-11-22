import QtQuick
import QtQuick.Controls

Button {
    id:control
    property bool primary:false
    implicitHeight:28
    implicitWidth:Math.max(28,contentItem.implicitWidth+20)
    padding:5;leftPadding:10;rightPadding:10
    focusPolicy:Qt.StrongFocus
    background:Rectangle {
        radius:4
        color:control.primary?(control.down?appTheme.colors.accentPressed:control.hovered?appTheme.colors.accentHover:appTheme.colors.accent):appTheme.backgrounds[control.down?"controlPressed":control.hovered?"controlHover":"control"]
        border.color:control.activeFocus?appTheme.colors.focusStrong:appTheme.colors.border
        border.width:control.activeFocus?1:0
        opacity:control.enabled?1:.4
    }
    contentItem:Text {
        text:control.text;color:control.enabled?(control.primary?appTheme.colors.accentText:appTheme.colors.text):appTheme.colors.disabled
        font:control.font;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
    }
}
