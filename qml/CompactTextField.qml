import QtQuick
import QtQuick.Controls

TextField {
    id:control
    implicitHeight:28;padding:6
    color:appTheme.colors.text;placeholderTextColor:appTheme.colors.placeholder
    selectionColor:appTheme.colors.accent;selectedTextColor:appTheme.colors.accentText
    selectByMouse:true
    background:Rectangle {
        color:appTheme.backgrounds.panel;radius:4
        border.color:control.activeFocus?appTheme.colors.focus:appTheme.colors.border
    }
}
