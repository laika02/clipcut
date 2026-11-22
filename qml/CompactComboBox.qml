import QtQuick
import QtQuick.Controls

ComboBox {
    id:control
    implicitHeight:28
    implicitWidth:160
    leftPadding:9;rightPadding:25;topPadding:4;bottomPadding:4
    background:Rectangle{radius:4;color:control.hovered?appTheme.colors.controlHover:appTheme.colors.control;border.color:control.activeFocus?appTheme.colors.focus:appTheme.colors.border}
    contentItem:Text {
        text:control.displayText;font:control.font;color:appTheme.colors.text
        verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight
    }
    indicator:Text{x:control.width-width-9;y:(control.height-height)/2;text:"▾";color:appTheme.colors.muted;font.pixelSize:12}
    delegate:ItemDelegate {
        required property int index
        width:control.width-6;height:28
        highlighted:control.highlightedIndex===index
        contentItem:Text{text:control.textAt(index);font:control.font;color:appTheme.colors.text;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
        background:Rectangle{radius:3;color:parent.highlighted?appTheme.colors.selection:parent.hovered?appTheme.colors.controlHover:"transparent"}
    }
    popup:Popup {
        y:control.height+4;width:control.width;padding:3
        implicitHeight:Math.min(contentItem.implicitHeight+6,260)
        background:Rectangle{color:appTheme.popupBackgrounds.panel;radius:4;border.color:appTheme.colors.borderStrong}
        contentItem:ListView {
            clip:true;implicitHeight:contentHeight
            model:control.popup.visible?control.delegateModel:null
            currentIndex:control.highlightedIndex
            ScrollIndicator.vertical:ScrollIndicator{}
        }
    }
}
