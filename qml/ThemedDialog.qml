import QtQuick
import QtQuick.Controls

Dialog {
    background:Rectangle{color:appTheme.popupBackgrounds.panel;radius:4;border.color:appTheme.colors.border}
    Overlay.modal:Rectangle{color:appTheme.withAlpha(appTheme.colors.mask,.25)}
}
