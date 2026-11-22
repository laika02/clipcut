import QtQuick
import QtQuick.Controls
Item {
    id: timeline
    property real viewStart: 0
    property real viewEnd: 1
    property bool editable: true
    property int track: -1
    objectName:track>=0?"audioTimeline":"editTimeline"
    signal editFocused()
    property var wave: []
    property real duration: Math.max(.01, editor.state.duration)
    function px(time) { return (time-viewStart)/Math.max(.001,viewEnd-viewStart)*width }
    function time(x) { return Math.max(0,Math.min(duration,viewStart+x/width*(viewEnd-viewStart))) }
    function focusSelectedDelete(kind) {
        for(let i=0;i<(editor.state.deletes||[]).length;++i)if(editor.state.deletes[i].selected){if(kind<0)deleteBodies.itemAt(i).forceActiveFocus();else deleteEdges.itemAt(i).focusEdge(kind);return}
    }
    clip: true
    Rectangle { anchors.fill: parent; radius: 3; color: appTheme.backgrounds.panel; border.color: appTheme.colors.border }
    Canvas {
        id: envelope; anchors.fill: parent; anchors.margins: 2
        onPaint: {
            const c=getContext("2d"); c.clearRect(0,0,width,height)
            c.strokeStyle=appTheme.colors.waveform; c.lineWidth=1; c.beginPath()
            if(timeline.wave.length) {
                for(let x=0;x<width;x+=2) {
                    const from=Math.max(0,Math.floor((viewStart+x/width*(viewEnd-viewStart))/duration*timeline.wave.length))
                    const to=Math.min(timeline.wave.length-1,Math.ceil((viewStart+(x+2)/width*(viewEnd-viewStart))/duration*timeline.wave.length))
                    let peak=0; for(let i=from;i<=to;i++)peak=Math.max(peak,timeline.wave[i])
                    const amp=Math.max(1,Math.min(1,peak*4)*(height*.43))
                    c.moveTo(x,height/2-amp);c.lineTo(x,height/2+amp)
                }
            } else {c.moveTo(0,height/2);c.lineTo(width,height/2)}
            c.stroke()
        }
        Connections { target: timeline; function onWaveChanged(){envelope.requestPaint()} function onViewStartChanged(){envelope.requestPaint()} function onViewEndChanged(){envelope.requestPaint()} }
        onWidthChanged: requestPaint()
        Connections{target:appTheme;function onChanged(){envelope.requestPaint()}}
    }
    MouseArea {
        anchors.fill: parent
        property bool creating:false
        property real pressX:0;property real start:0
        property int createdIndex:-1
        function createTo(x) {
            if(createdIndex<0&&Math.abs(x-pressX)<3)return
            const end=timeline.time(x)
            if(createdIndex<0)createdIndex=timeline.track>=0?editor.addMute(timeline.track,start,end):editor.addDelete(start,end)
            else if(timeline.track>=0)editor.setMute(timeline.track,createdIndex,start,end)
            else editor.setDelete(createdIndex,start,end)
        }
        onPressed:mouse=>{
            creating=!!(mouse.modifiers&Qt.ControlModifier);pressX=mouse.x;start=timeline.time(mouse.x);createdIndex=-1
            if(timeline.track>=0)editor.activateTrack(timeline.track)
            if(creating){timeline.forceActiveFocus();editor.beginGesture()}
            else editor.scrub(start,timeline.track,false)
        }
        onPositionChanged:mouse=>{if(pressed){if(creating)createTo(mouse.x);else editor.scrub(timeline.time(mouse.x),timeline.track,false)}}
        onReleased:mouse=>{if(creating){createTo(mouse.x);editor.endGesture();creating=false}else editor.scrub(timeline.time(mouse.x),timeline.track,true)}
        onCanceled:{if(creating){editor.endGesture();creating=false}else editor.scrub(preview.position,timeline.track,true)}
        onWheel: wheel=>{if(wheel.modifiers&Qt.ControlModifier){editor.dispatch(wheel.angleDelta.y>0?"zoomIn":"zoomOut");wheel.accepted=true}else wheel.accepted=false}
    }
    Rectangle { x:0;width:Math.max(0,timeline.px(editor.state.trimStart));height:parent.height;color:appTheme.withAlpha(appTheme.colors.mask,0.69) }
    Rectangle { x:timeline.px(editor.state.trimEnd);width:Math.max(0,parent.width-x);height:parent.height;color:appTheme.withAlpha(appTheme.colors.mask,0.69) }
    Repeater {
        id:deleteBodies
        model: (editor.state.deletes || []).length
        Rectangle {
            id:region
            objectName:"deleteRegion"
            activeFocusOnTab:timeline.editable
            required property int index
            property var modelData:editor.state.deletes[index]||{start:0,end:0,index:-1,selected:false}
            x:timeline.px(modelData.start);width:Math.max(1,timeline.px(modelData.end)-x);height:timeline.height
            color:modelData.selected?appTheme.withAlpha(appTheme.colors.cutFill,0.5):appTheme.withAlpha(appTheme.colors.cutFill,0.375);border.color:modelData.selected?appTheme.colors.cut:appTheme.colors.cutBorder
            Canvas {
                id:hatching
                anchors.fill:parent;visible:timeline.editable
                onPaint:{const c=getContext("2d");c.clearRect(0,0,width,height);c.strokeStyle=appTheme.withAlpha(appTheme.colors.cut,0.19);c.lineWidth=1;c.beginPath();for(let x=-height;x<width;x+=10){c.moveTo(x,0);c.lineTo(x+height,height)}c.stroke()}
                onWidthChanged:requestPaint();onHeightChanged:requestPaint()
                Connections{target:appTheme;function onChanged(){hatching.requestPaint()}}
            }
            Label { anchors.centerIn:parent;text:"×";color:appTheme.colors.cutText;visible:timeline.editable&&parent.width>14 }
            Keys.onPressed:event=>{
                const refocus=timeline.focusSelectedDelete
                if(event.key===Qt.Key_Left||event.key===Qt.Key_Right){editor.selectDelete(modelData.index);editor.moveDelete(modelData.index,modelData.start+(event.key===Qt.Key_Left?-1:1)*(event.modifiers&Qt.ShiftModifier?10:1)/Math.max(1,editor.state.fps));Qt.callLater(refocus,-1);event.accepted=true}
                else if(event.key===Qt.Key_Delete){timeline.forceActiveFocus();editor.selectDelete(modelData.index);editor.dispatch("removeDelete");event.accepted=true}
            }
            MouseArea {
                anchors.fill:parent;acceptedButtons:timeline.editable?Qt.LeftButton|Qt.RightButton:Qt.RightButton;cursorShape:timeline.editable?(pressed?Qt.ClosedHandCursor:Qt.OpenHandCursor):Qt.ArrowCursor
                property real pressX:0
                property real start:0
                onPressed:mouse=>{if(mouse.button===Qt.RightButton)return;if(mouse.modifiers&Qt.ControlModifier){mouse.accepted=false;return}region.forceActiveFocus();pressX=mapToItem(timeline,mouse.x,mouse.y).x;start=modelData.start;editor.selectDelete(modelData.index);editor.beginGesture()}
                onPositionChanged:mouse=>{if(pressed&&(pressedButtons&Qt.LeftButton)){const delta=(mapToItem(timeline,mouse.x,mouse.y).x-pressX)/timeline.width*(timeline.viewEnd-timeline.viewStart);editor.moveDelete(modelData.index,start+delta)}}
                onReleased:mouse=>{if(mouse.button===Qt.LeftButton){const refocus=timeline.focusSelectedDelete;editor.endGesture();Qt.callLater(refocus,-1)}};onCanceled:editor.endGesture()
                onClicked:mouse=>{if(mouse.button===Qt.RightButton){timeline.forceActiveFocus();editor.selectDelete(modelData.index);editor.dispatch("removeDelete")}}
            }
        }
    }
    MuteRegions{visible:timeline.track>=0;owner:timeline;track:timeline.track;onEditFocused:timeline.editFocused()}
    Rectangle { x:timeline.px(preview.position);width:1;height:parent.height;color:appTheme.colors.text }
    component Edge: Rectangle {
        id:edge
        objectName:"editHandle"
        activeFocusOnTab:true
        property real at:0
        property int kind:0
        property int deleteIndex:-1
        property real other:0
        x:timeline.px(at)-6;width:12;height:timeline.height
        color:activeFocus?appTheme.withAlpha(appTheme.colors.border,0.25):"transparent"
        property color gripColor:kind<2?appTheme.colors.focus:appTheme.colors.cut
        Rectangle { anchors.horizontalCenter:parent.horizontalCenter;width:2;height:parent.height;color:edge.gripColor }
        Rectangle { anchors.horizontalCenter:parent.horizontalCenter;anchors.top:parent.top;width:8;height:4;radius:1;color:edge.gripColor }
        Rectangle { anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;width:8;height:4;radius:1;color:edge.gripColor }
        function change(value) {
            if(kind===0)editor.trim(Math.min(value,editor.state.trimEnd-1/Math.max(1,editor.state.fps)),editor.state.trimEnd)
            else if(kind===1)editor.trim(editor.state.trimStart,Math.max(value,editor.state.trimStart+1/Math.max(1,editor.state.fps)))
            else if(kind===2)editor.setDelete(deleteIndex,Math.min(value,other),other)
            else editor.setDelete(deleteIndex,other,Math.max(value,other))
        }
        Keys.onPressed: event=>{
            const refocus=timeline.focusSelectedDelete,part=kind
            if(event.key===Qt.Key_Left || event.key===Qt.Key_Right){if(deleteIndex>=0)editor.selectDelete(deleteIndex);change(at+(event.key===Qt.Key_Left?-1:1)*(event.modifiers&Qt.ShiftModifier?10:1)/Math.max(1,editor.state.fps));if(part>=2)Qt.callLater(refocus,part);event.accepted=true}
            if(event.key===Qt.Key_Delete && deleteIndex>=0){timeline.forceActiveFocus();editor.selectDelete(deleteIndex);editor.dispatch("removeDelete");event.accepted=true}
        }
        MouseArea {
            anchors.fill:parent;cursorShape:Qt.SizeHorCursor
            acceptedButtons:edge.deleteIndex>=0?Qt.LeftButton|Qt.RightButton:Qt.LeftButton
            onPressed:mouse=>{if(mouse.button===Qt.RightButton)return;if(mouse.modifiers&Qt.ControlModifier){mouse.accepted=false;return}edge.forceActiveFocus();editor.beginGesture();if(edge.deleteIndex>=0)editor.selectDelete(edge.deleteIndex)}
            onPositionChanged: mouse=>{if(pressed&&(pressedButtons&Qt.LeftButton))edge.change(timeline.time(mapToItem(timeline,mouse.x,mouse.y).x))}
            onReleased:mouse=>{if(mouse.button===Qt.LeftButton){const kind=edge.kind,refocus=timeline.focusSelectedDelete;editor.endGesture();if(kind>=2)Qt.callLater(refocus,kind)}};onCanceled:editor.endGesture()
            onClicked:mouse=>{if(mouse.button===Qt.RightButton&&edge.deleteIndex>=0){timeline.forceActiveFocus();editor.selectDelete(edge.deleteIndex);editor.dispatch("removeDelete")}}
        }
        ToolTip.visible: mouseHover.hovered
        ToolTip.text: "Edge · arrows one frame · Shift ten"
        HoverHandler { id:mouseHover }
    }
    Edge { visible:editable;at:editor.state.trimStart||0;kind:0 }
    Edge { visible:editable;at:editor.state.trimEnd||0;kind:1 }
    Repeater {
        id:deleteEdges
        model:editable?(editor.state.deletes||[]).length:0
        Item {
            required property int index
            property var modelData:editor.state.deletes[index]||{start:0,end:0,index:-1,selected:false}
            width:timeline.width;height:timeline.height
            function focusEdge(kind){if(kind===2)startEdge.forceActiveFocus();else endEdge.forceActiveFocus()}
            Edge { id:startEdge;at:modelData.start;kind:2;deleteIndex:modelData.index;other:modelData.end }
            Edge { id:endEdge;at:modelData.end;kind:3;deleteIndex:modelData.index;other:modelData.start }
        }
    }
}
