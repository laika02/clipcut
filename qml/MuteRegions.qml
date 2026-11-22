import QtQuick
import QtQuick.Controls
Item {
    id:overlay
    required property Item owner
    required property int track
    signal editFocused()
    property var regions:editor.state.tracks[track]?editor.state.tracks[track].mutes:[]
    anchors.fill:parent
    function focusSelected(part) {
        for(let i=0;i<sections.count;++i){const section=sections.itemAt(i);if(section&&section.regionData.selected){section.focusPart(part);return}}
    }
    Connections {
        target:editor
        function onMuteDragFinished(track,part){if(track===overlay.track)Qt.callLater(overlay.focusSelected,part)}
    }
    Repeater {
        id:sections
        model:(overlay.regions||[]).length
        Item {
            id:region
            required property int index
            property var regionData:overlay.regions[index]||{start:0,end:0,selected:false}
            x:overlay.owner.px(regionData.start);width:Math.max(1,overlay.owner.px(regionData.end)-x);height:overlay.height
            objectName:"muteRegion";activeFocusOnTab:true
            function focusPart(part){if(part<0)forceActiveFocus();else handles.itemAt(part).forceActiveFocus()}
            onActiveFocusChanged:if(activeFocus){editor.activateTrack(overlay.track);overlay.editFocused()}
            Rectangle{anchors.fill:parent;color:appTheme.withAlpha(appTheme.colors.accent,region.regionData.selected ? .26 : .15);border.color:appTheme.colors.accent}
            Label{anchors.centerIn:parent;text:"mute";font.pixelSize:9;color:appTheme.colors.text;visible:parent.width>38}
            Keys.onPressed:event=>{
                const refocus=overlay.focusSelected,part=index
                if(event.key===Qt.Key_Left||event.key===Qt.Key_Right){editor.selectMute(overlay.track,index);editor.moveMute(overlay.track,index,regionData.start+(event.key===Qt.Key_Left?-1:1)*(event.modifiers&Qt.ShiftModifier?10:1)/Math.max(1,editor.state.fps));Qt.callLater(refocus,-1);event.accepted=true}
                else if(event.key===Qt.Key_Delete){overlay.owner.forceActiveFocus();editor.removeMute(overlay.track,index);event.accepted=true}
            }
            MouseArea {
                anchors.fill:parent;cursorShape:pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor
                acceptedButtons:Qt.LeftButton|Qt.RightButton
                preventStealing:true
                onPressed:mouse=>{
                    if(mouse.button===Qt.RightButton)return
                    if(mouse.modifiers&Qt.ControlModifier){mouse.accepted=false;return}
                    const p=mapToItem(overlay.owner,mouse.x,mouse.y)
                    region.forceActiveFocus();editor.beginMuteDrag(overlay.track,region.index,-1,overlay.owner,p.x)
                }
                onDoubleClicked:mouse=>{if(mouse.button===Qt.LeftButton)mouse.accepted=false}
                onClicked:mouse=>{if(mouse.button===Qt.RightButton){overlay.owner.forceActiveFocus();editor.removeMute(overlay.track,region.index)}}
                onCanceled:editor.noteMuteGrabLoss()
            }
            Repeater {
                id:handles
                model:2
                Rectangle {
                    id:edge
                    required property int index
                    readonly property bool endEdge:index===1
                    readonly property real at:endEdge?region.regionData.end:region.regionData.start
                    objectName:"muteHandle";activeFocusOnTab:true
                    onActiveFocusChanged:if(activeFocus){editor.activateTrack(overlay.track);overlay.editFocused()}
                    x:(endEdge?region.width:0)-6;width:12;height:region.height
                    color:activeFocus?appTheme.withAlpha(appTheme.colors.accent,.3):"transparent"
                    Rectangle{anchors.horizontalCenter:parent.horizontalCenter;width:2;height:parent.height;color:appTheme.colors.accent}
                    Rectangle{anchors.horizontalCenter:parent.horizontalCenter;width:7;height:3;color:appTheme.colors.accent}
                    function change(value){const minimum=1/Math.max(1,editor.state.fps);if(endEdge)editor.setMute(overlay.track,region.index,region.regionData.start,Math.max(value,region.regionData.start+minimum));else editor.setMute(overlay.track,region.index,Math.min(value,region.regionData.end-minimum),region.regionData.end)}
                    Keys.onPressed:event=>{
                const refocus=overlay.focusSelected,part=index
                        if(event.key===Qt.Key_Left||event.key===Qt.Key_Right){editor.selectMute(overlay.track,region.index);change(at+(event.key===Qt.Key_Left?-1:1)*(event.modifiers&Qt.ShiftModifier?10:1)/Math.max(1,editor.state.fps));Qt.callLater(refocus,part);event.accepted=true}
                        else if(event.key===Qt.Key_Delete){overlay.owner.forceActiveFocus();editor.removeMute(overlay.track,region.index);event.accepted=true}
                    }
                    MouseArea {
                        anchors.fill:parent;cursorShape:Qt.SizeHorCursor
                        acceptedButtons:Qt.LeftButton|Qt.RightButton
                        preventStealing:true
                        onPressed:mouse=>{
                            if(mouse.button===Qt.RightButton)return
                            if(mouse.modifiers&Qt.ControlModifier){mouse.accepted=false;return}
                            const p=mapToItem(overlay.owner,mouse.x,mouse.y)
                            edge.forceActiveFocus();editor.beginMuteDrag(overlay.track,region.index,edge.index,overlay.owner,p.x)
                        }
                        onDoubleClicked:mouse=>{if(mouse.button===Qt.LeftButton)mouse.accepted=false}
                        onClicked:mouse=>{if(mouse.button===Qt.RightButton){overlay.owner.forceActiveFocus();editor.removeMute(overlay.track,region.index)}}
                        onCanceled:editor.noteMuteGrabLoss()
                    }
                }
            }
        }
    }
}
