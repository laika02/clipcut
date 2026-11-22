import QtQuick
Item {
    id:overlay
    property Item inputSurface:parent
    property point inputOrigin:Qt.point(x,y)
    property real sx:width/Math.max(1,editor.state.width)
    property real sy:height/Math.max(1,editor.state.height)
    property real cropLeft:editor.state.cropX*sx
    property real cropTop:editor.state.cropY*sy
    property real cw:editor.state.cropW*sx
    property real ch:editor.state.cropH*sy
    readonly property real sourceRight:Math.floor(editor.state.width/2)*2
    readonly property real sourceBottom:Math.floor(editor.state.height/2)*2
    function aligned(value,minimum,maximum) {
        return Math.max(minimum,Math.min(maximum,Math.round(value/2)*2))
    }
    // Modes 0–3 are corners; 4/5 are left/right; 6/7 are top/bottom.
    function resizeHandle(mode,left,top,right,bottom,dx,dy) {
        // Align the moving edges before calculating size, keeping the opposite
        // corner exact even when the encoder requires even crop coordinates.
        const minWidth=Math.min(16,sourceRight),minHeight=Math.min(16,sourceBottom)
        if(mode<4||mode===4||mode===5) {
            if(mode===5||(mode<4&&mode%2))right=aligned(right+dx,left+minWidth,sourceRight)
            else left=aligned(left+dx,0,right-minWidth)
        }
        if(mode<4||mode===6||mode===7) {
            if(mode===7||(mode<4&&mode>=2))bottom=aligned(bottom+dy,top+minHeight,sourceBottom)
            else top=aligned(top+dy,0,bottom-minHeight)
        }
        editor.crop(left,top,right-left,bottom-top)
    }
    Rectangle{x:0;y:0;width:parent.width;height:overlay.cropTop;color:appTheme.withAlpha(appTheme.colors.mask,0.4)}
    Rectangle{x:0;y:overlay.cropTop+overlay.ch;width:parent.width;height:parent.height-y;color:appTheme.withAlpha(appTheme.colors.mask,0.4)}
    Rectangle{x:0;y:overlay.cropTop;width:overlay.cropLeft;height:overlay.ch;color:appTheme.withAlpha(appTheme.colors.mask,0.4)}
    Rectangle{x:overlay.cropLeft+overlay.cw;y:overlay.cropTop;width:parent.width-x;height:overlay.ch;color:appTheme.withAlpha(appTheme.colors.mask,0.4)}
    Rectangle {
        x:overlay.cropLeft;y:overlay.cropTop;width:overlay.cw;height:overlay.ch;color:"transparent";border.width:1;border.color:appTheme.colors.focus
    }
    Repeater {
        id:corners
        model:4
        Rectangle {
            id:corner;required property int index
            objectName:"editHandle"
            activeFocusOnTab:true
            x:overlay.cropLeft+(index%2?overlay.cw:0)-8;y:overlay.cropTop+(index>=2?overlay.ch:0)-8
            width:16;height:16;color:activeFocus?appTheme.withAlpha(appTheme.colors.selection,0.5):"transparent";radius:3
            Rectangle {anchors.centerIn:parent;width:6;height:6;radius:1;color:appTheme.colors.focus}
            Keys.onPressed:event=>{
                const step=event.modifiers&Qt.ShiftModifier?10:2
                let dx=0,dy=0
                if(event.key===Qt.Key_Left)dx=-step;else if(event.key===Qt.Key_Right)dx=step;else if(event.key===Qt.Key_Up)dy=-step;else if(event.key===Qt.Key_Down)dy=step;else return
                overlay.resizeHandle(index,editor.state.cropX,editor.state.cropY,editor.state.cropX+editor.state.cropW,editor.state.cropY+editor.state.cropH,dx,dy)
                event.accepted=true
            }
        }
    }
    Repeater {
        id:edges
        model:4
        Rectangle {
            required property int index
            readonly property int cropEdge:index+4
            readonly property bool horizontal:index<2
            objectName:"editHandle";activeFocusOnTab:true
            x:overlay.cropLeft+(horizontal?(index===1?overlay.cw:0):overlay.cw/2)-8
            y:overlay.cropTop+(horizontal?overlay.ch/2:(index===3?overlay.ch:0))-8
            width:16;height:16;radius:3
            color:activeFocus?appTheme.withAlpha(appTheme.colors.selection,0.5):"transparent"
            Rectangle{anchors.centerIn:parent;width:parent.horizontal?2:12;height:parent.horizontal?12:2;radius:1;color:appTheme.colors.focus}
            Keys.onPressed:event=>{
                const step=event.modifiers&Qt.ShiftModifier?10:2
                let dx=0,dy=0
                if(event.key===Qt.Key_Left)dx=-step;else if(event.key===Qt.Key_Right)dx=step;else if(event.key===Qt.Key_Up)dy=-step;else if(event.key===Qt.Key_Down)dy=step;else return
                overlay.resizeHandle(cropEdge,editor.state.cropX,editor.state.cropY,editor.state.cropX+editor.state.cropW,editor.state.cropY+editor.state.cropH,dx,dy)
                event.accepted=true
            }
        }
    }
    // Input stays attached to the video, never to the changing crop geometry.
    // Include the outer half of handles on the full-frame crop boundary.
    MouseArea {
        // Receive edge hits through the preview pane: a full-frame handle
        // extends beyond the video item's own input bounds.
        parent:overlay.inputSurface
        visible:overlay.visible
        x:overlay.inputOrigin.x-8;y:overlay.inputOrigin.y-8
        width:overlay.width+16;height:overlay.height+16;z:3
        hoverEnabled:true;preventStealing:true
        property int dragMode:-2
        property real pressX:0;property real pressY:0
        property real startLeft:0;property real startTop:0
        property real startRight:0;property real startBottom:0
        property real startSx:1;property real startSy:1
        function hitTest(x,y) {
            let closest=-2,distance=Infinity
            for(let i=0;i<4;++i) {
                const dx=x-overlay.cropLeft-(i%2?overlay.cw:0),dy=y-overlay.cropTop-(i>=2?overlay.ch:0)
                if(Math.abs(dx)<=8&&Math.abs(dy)<=8&&dx*dx+dy*dy<distance){closest=i;distance=dx*dx+dy*dy}
            }
            if(closest>=0)return closest
            // Corners take priority where their targets overlap an edge.
            if(y>=overlay.cropTop&&y<=overlay.cropTop+overlay.ch) {
                if(Math.abs(x-overlay.cropLeft)<=8)return 4
                if(Math.abs(x-overlay.cropLeft-overlay.cw)<=8)return 5
            }
            if(x>=overlay.cropLeft&&x<=overlay.cropLeft+overlay.cw) {
                if(Math.abs(y-overlay.cropTop)<=8)return 6
                if(Math.abs(y-overlay.cropTop-overlay.ch)<=8)return 7
            }
            return x>=overlay.cropLeft&&x<=overlay.cropLeft+overlay.cw&&y>=overlay.cropTop&&y<=overlay.cropTop+overlay.ch?-1:-2
        }
        cursorShape: {
            const p=mapToItem(overlay,mouseX,mouseY),mode=pressed?dragMode:hitTest(p.x,p.y)
            if(mode>=4)return mode<6?Qt.SizeHorCursor:Qt.SizeVerCursor
            return mode>=0?(mode===0||mode===3?Qt.SizeFDiagCursor:Qt.SizeBDiagCursor):mode===-1?Qt.SizeAllCursor:Qt.ArrowCursor
        }
        function moveTo(x,y) {
            if(dragMode===-2)return
            const dx=(x-pressX)/startSx,dy=(y-pressY)/startSy
            if(dragMode>=0)overlay.resizeHandle(dragMode,startLeft,startTop,startRight,startBottom,dx,dy)
            else {
                const w=startRight-startLeft,h=startBottom-startTop
                editor.crop(overlay.aligned(startLeft+dx,0,overlay.sourceRight-w),overlay.aligned(startTop+dy,0,overlay.sourceBottom-h),w,h)
            }
        }
        function finish() {
            if(dragMode!==-2){dragMode=-2;editor.endGesture()}
        }
        onPressed:mouse=>{
            const p=mapToItem(overlay,mouse.x,mouse.y)
            dragMode=hitTest(p.x,p.y)
            if(dragMode===-2){mouse.accepted=false;return}
            pressX=p.x;pressY=p.y;startSx=overlay.sx;startSy=overlay.sy
            startLeft=editor.state.cropX;startTop=editor.state.cropY
            startRight=startLeft+editor.state.cropW;startBottom=startTop+editor.state.cropH
            if(dragMode>=4)edges.itemAt(dragMode-4).forceActiveFocus()
            else if(dragMode>=0)corners.itemAt(dragMode).forceActiveFocus()
            else overlay.forceActiveFocus()
            editor.beginGesture()
        }
        onPositionChanged:mouse=>{if(pressed){const p=mapToItem(overlay,mouse.x,mouse.y);moveTo(p.x,p.y)}}
        onReleased:mouse=>{const p=mapToItem(overlay,mouse.x,mouse.y);moveTo(p.x,p.y);finish()}
        onCanceled:finish()
        onDoubleClicked:mouse=>{if(dragMode===-1){finish();editor.dispatch("resetCrop")}}
    }
}
