import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
ApplicationWindow {
    id:root
    width:1180;height:780;minimumWidth:800;minimumHeight:420
    visible:true;title:"ClipCut"+(editor.state.name?" · "+editor.state.name:"");color:"transparent"
    flags:Qt.platform.os==="windows"?Qt.Window|Qt.FramelessWindowHint:Qt.Window
    background:Rectangle{color:appTheme.backgrounds.window;radius:Qt.platform.os==="windows"?4:0}
    font.family:appTheme.fontFamily;font.pixelSize:12
    palette.window:appTheme.colors.window;palette.base:appTheme.colors.panel;palette.text:appTheme.colors.text;palette.button:appTheme.colors.control;palette.buttonText:appTheme.colors.text;palette.highlight:appTheme.colors.accent
    palette.mid:appTheme.colors.border;palette.dark:appTheme.colors.border;palette.light:appTheme.colors.border
    palette.windowText:appTheme.colors.text;palette.highlightedText:appTheme.colors.accentText
    property real viewStart:0
    property real viewEnd:Math.max(1,editor.state.duration)
    property bool allowClose:false
    property string pendingAction:""
    property int extractTrack:-1
    property bool extractOriginal:false
    property string message:""
    property string loadedPath:""
    property bool extractionPopupOpen:false
    property var outputOptions:({})
    property url proposedOutput:""
    property string appearanceError:""
    TextMetrics{id:extractMetrics;font:root.font;text:"Extract"}
    readonly property real audioActionWidth:Math.max(28,extractMetrics.width+20)
    function resizePreview(delta){editPane.SplitView.preferredHeight=Math.max(134,Math.min(workspace.height-viewport.SplitView.minimumHeight-10,editPane.height-delta))}
    function beginOutput(options){outputOptions=options;outputName.open()}
    function saveNamedOutput(){
        const result=editor.namedOutput(exportName.text,outputOptions.compress||false,outputOptions.extractTrack===undefined?-1:outputOptions.extractTrack,outputOptions.original||false)
        if(result.error){outputName.error=result.error;return}
        proposedOutput=result.url
        if(files.exists(proposedOutput)){replaceOutput.open();return}
        commitOutput()
    }
    function commitOutput(){const options=Object.assign({},outputOptions,{overwrite:true});outputName.close();exportSheet.close();editor.startExport(proposedOutput,options)}
    function clock(t) {if(!isFinite(t))return "00:00.000";const s=Math.max(0,t);return(s>=3600?Math.floor(s/3600)+":":"")+String(Math.floor(s/60)%60).padStart(2,"0")+":"+String(Math.floor(s)%60).padStart(2,"0")+"."+String(Math.floor(s*1000)%1000).padStart(3,"0")}
    function zoom(factor) {const span=Math.max(1,Math.min(editor.state.duration,(viewEnd-viewStart)*factor));viewStart=Math.max(0,Math.min(editor.state.duration-span,preview.position-span/2));viewEnd=viewStart+span}
    function requestOpen(){if(editor.state.dirty){pendingAction="open";warning.open()}else openFile.open()}
    function revealAudio(row){editor.activateTrack(row.index);const f=audioScroll.contentItem;if(f&&f.contentY!==undefined){const p=row.mapToItem(f.contentItem,0,0);if(p.y<f.contentY)f.contentY=p.y;else if(p.y+row.height>f.contentY+f.height)f.contentY=p.y+row.height-f.height}}
    function beginExtract(index,original){extractTrack=index;extractOriginal=original;beginOutput({extractTrack:index,original:original})}
    onClosing:close=>{if(editor.job.running||editor.state.dirty){if(!allowClose){close.accepted=false;pendingAction="close";warning.open()}}}
    ColumnLayout {
        anchors.fill:parent;anchors.margins:14;spacing:9
        RowLayout {
            Layout.fillWidth:true;spacing:8
            Label{objectName:"brandLabel";text:"clipcut";font.pixelSize:18;font.weight:Font.DemiBold}
            Label{text:"0.1";font.pixelSize:10;color:appTheme.colors.accent}
            Label {
                Layout.fillWidth:true;text:editor.state.loading?"Opening…":editor.state.name||"Video surgery";color:appTheme.colors.muted;elide:Text.ElideMiddle
                MouseArea{anchors.fill:parent;onPressed:root.startSystemMove();onDoubleClicked:root.visibility===Window.Maximized?root.showNormal():root.showMaximized()}
            }
            SmallButton{text:"Open";onClicked:root.requestOpen();ToolTip.text:"Open · Ctrl+O";ToolTip.visible:hovered}
            SmallButton{primary:true;text:"Export";enabled:editor.state.loaded&&!editor.job.running;onClicked:{extractTrack=-1;exportSheet.open()}}
            SmallButton{text:"⋯";onClicked:settings.open();ToolTip.text:"Settings · Ctrl+,";ToolTip.visible:hovered}
            SmallButton{visible:Qt.platform.os==="windows";text:"−";onClicked:root.showMinimized();ToolTip.text:"Minimize";ToolTip.visible:hovered}
            SmallButton{visible:Qt.platform.os==="windows";text:root.visibility===Window.Maximized?"▣":"□";onClicked:root.visibility===Window.Maximized?root.showNormal():root.showMaximized();ToolTip.text:"Maximize / restore";ToolTip.visible:hovered}
            SmallButton{visible:Qt.platform.os==="windows";text:"×";onClicked:root.close();ToolTip.text:"Close";ToolTip.visible:hovered}
        }
        SplitView {
            id:workspace;objectName:"workspaceSplit";Layout.fillWidth:true;Layout.fillHeight:true
            orientation:Qt.Vertical
            handle:Item {
                objectName:"previewDivider";implicitHeight:10
                Rectangle{anchors.centerIn:parent;width:36;height:2;radius:1;color:parent.SplitHandle.pressed?appTheme.colors.focus:parent.SplitHandle.hovered?appTheme.colors.accent:appTheme.colors.divider}
                ToolTip.visible:SplitHandle.hovered
                ToolTip.text:"Resize preview · drag or Alt+Shift+Up/Down"
            }
            Item {
                id:viewport;objectName:"previewPane";SplitView.fillHeight:true;SplitView.minimumHeight:editor.job.running||editor.state.exportState?72:100
                Rectangle{anchors.fill:parent;color:appTheme.backgrounds.videoBackground;radius:5}
                Item {
                    id:picture
                    property real aspect:editor.state.width>0?editor.state.width*editor.state.sar/editor.state.height:16/9
                    // Keep the complete crop handle inside the preview pane,
                    // including its clickable half outside a full-frame crop.
                    width:Math.max(0,Math.min(viewport.width-16,(viewport.height-16)*aspect));height:width/aspect;anchors.centerIn:parent
                    Component.onCompleted:{const video=Qt.createQmlObject(videoComponent,picture,"VideoSurface");video.anchors.fill=picture;Qt.callLater(()=>preview.attach(video))}
                    CropOverlay{objectName:"cropOverlay";z:2;anchors.fill:parent;inputSurface:viewport;inputOrigin:Qt.point(picture.x,picture.y);visible:editor.state.loaded}
                }
                Label{anchors.centerIn:parent;visible:!editor.state.loaded;text:editor.state.loading?"Opening recording…":"Open a recording to begin";color:appTheme.colors.muted}
                Label{anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:8;visible:editor.state.loaded;text:editor.state.cropW+" × "+editor.state.cropH;color:appTheme.colors.muted;font.pixelSize:10}
            }
            Item {
                id:editPane;objectName:"editPane"
                SplitView.minimumHeight:134
                SplitView.preferredHeight:Math.max(134,Math.min(300,root.height<550?146:root.height*.38))
                ColumnLayout {
                    anchors.fill:parent;spacing:6
                    RowLayout {
                        Layout.fillWidth:true;spacing:8
                        SmallButton{text:preview.playing?"Ⅱ":"▶";enabled:editor.state.loaded;onClicked:editor.dispatch("play")}
                        Label{text:root.clock(preview.position);font.family:"monospace"}
                        Label{text:"/ "+root.clock(editor.state.duration);color:appTheme.colors.muted}
                        Item{Layout.fillWidth:true}
                        Label{text:"Kept "+root.clock(editor.state.keptDuration);color:appTheme.colors.muted}
                        SmallButton{text:"−";onClicked:root.zoom(2);enabled:editor.state.loaded;ToolTip.text:"Zoom timeline out · −";ToolTip.visible:hovered}
                        SmallButton{text:"+";onClicked:root.zoom(.5);enabled:editor.state.loaded;ToolTip.text:"Zoom timeline in · +";ToolTip.visible:hovered}
                        SmallButton{text:"Fit";onClicked:{viewStart=0;viewEnd=Math.max(1,editor.state.duration)}ToolTip.text:"Fit whole recording on timeline · F";ToolTip.visible:hovered}
                    }
                    RowLayout {
                        Layout.fillWidth:true;spacing:0
                        Label{text:"TRIM / DELETES";font.pixelSize:10;color:appTheme.colors.muted;Layout.preferredWidth:180}
                        Timeline{Layout.fillWidth:true;Layout.preferredHeight:32;viewStart:root.viewStart;viewEnd:root.viewEnd;enabled:editor.state.loaded}
                        Item{Layout.preferredWidth:root.audioActionWidth+6}
                    }
                    RowLayout {
                        Layout.fillWidth:true;spacing:0
                        Item{Layout.preferredWidth:180}
                        TimelineRuler{Layout.fillWidth:true;Layout.preferredHeight:20;viewStart:root.viewStart;viewEnd:root.viewEnd}
                        Item{Layout.preferredWidth:root.audioActionWidth+6}
                    }
                    ScrollView {
                        id:audioScroll;objectName:"audioScroll";Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:36;clip:true;contentWidth:availableWidth
                        ScrollBar.vertical:ScrollBar {
                            objectName:"audioScrollBar"
                            parent:audioScroll
                            anchors.top:parent.top;anchors.bottom:parent.bottom;anchors.right:parent.right
                            anchors.rightMargin:root.audioActionWidth
                            policy:ScrollBar.AsNeeded;active:true;width:6
                            contentItem:Rectangle{implicitWidth:4;implicitHeight:24;radius:2;color:appTheme.colors.accent;opacity:.85}
                        }
                        ScrollBar.horizontal.policy:ScrollBar.AlwaysOff
                        Column {
                            width:audioScroll.availableWidth;spacing:4
                            Repeater {
                                model:(editor.state.tracks||[]).length
                                RowLayout {
                                    required property int index
                                    property var modelData:editor.state.tracks[index]
                                    id:row;width:parent.width;height:36;spacing:6
                                    CompactCheckBox{checked:modelData.included;implicitWidth:22;onClicked:editor.include(modelData.index);onActiveFocusChanged:if(activeFocus)root.revealAudio(row);ToolTip.text:"Include · Ctrl+"+(modelData.index+1);ToolTip.visible:hovered}
                                    Label{text:modelData.title;elide:Text.ElideRight;Layout.preferredWidth:94;color:modelData.included?appTheme.colors.text:appTheme.colors.muted}
                                    SmallButton{text:modelData.solo?"Mix":"Solo";Layout.preferredWidth:46;onClicked:editor.solo(modelData.index,true);onActiveFocusChanged:if(activeFocus)root.revealAudio(row)}
                                    Timeline{Layout.fillWidth:true;Layout.preferredHeight:30;viewStart:root.viewStart;viewEnd:root.viewEnd;editable:false;track:modelData.index;wave:modelData.wave;onEditFocused:root.revealAudio(row)}
                                    SmallButton {
                                        text:"Extract";Layout.preferredWidth:root.audioActionWidth
                                        onActiveFocusChanged:if(activeFocus)root.revealAudio(row)
                                        onClicked:{if(modelData.modified)extractPopup.open();else root.beginExtract(modelData.index,true)}
                                        Popup {
                                        id:extractPopup;y:parent.height+4;x:Math.min(0,parent.width-width);padding:5
                                            onOpened:root.extractionPopupOpen=true
                                            onClosed:root.extractionPopupOpen=false
                                            Column{spacing:3
                                                SmallButton{text:"Original track";onClicked:{extractPopup.close();root.beginExtract(modelData.index,true)}}
                                                SmallButton{text:"Edited track";onClicked:{extractPopup.close();root.beginExtract(modelData.index,false)}}
                                            }
                                        }
                                    }
                                    Shortcut{sequence:String(modelData.index+1);enabled:modelData.index<9&&!root.typing&&!root.modal;onActivated:editor.solo(modelData.index,true)}
                                    Shortcut{sequence:"Ctrl+"+(modelData.index+1);enabled:modelData.index<9&&!root.typing&&!root.modal;onActivated:editor.include(modelData.index)}
                                }
                            }
                            Label{visible:editor.state.loaded&&(editor.state.tracks||[]).length===0;text:"No audio tracks";color:appTheme.colors.muted;height:35}
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth:true
            Label{Layout.fillWidth:true;text:root.message||preview.error||"Ctrl-drag: delete / track mute · I / O trim · Ctrl+K commands";color:root.message||preview.error?appTheme.colors.focus:appTheme.colors.muted;font.pixelSize:11;elide:Text.ElideRight}
        }
        Rectangle {
            objectName:"exportStatus";visible:editor.job.running||!!editor.state.exportState
            Layout.fillWidth:true;Layout.preferredHeight:50;radius:4;color:appTheme.backgrounds.status;border.color:appTheme.colors.accent
            RowLayout {
                anchors.fill:parent;anchors.margins:8;spacing:10
                BusyIndicator{visible:editor.job.running;running:editor.job.running;implicitWidth:22;implicitHeight:22}
                ColumnLayout {
                    Layout.fillWidth:true;spacing:2
                    Label{Layout.fillWidth:true;font.weight:Font.DemiBold;color:appTheme.colors.text;text:editor.job.running?"Exporting · "+Math.round(editor.job.progress*100)+"%":editor.state.exportState==="done"?"✓ Export complete":editor.state.exportState==="cancelled"?"Export cancelled":"Export failed"}
                    Label{Layout.fillWidth:true;color:appTheme.colors.muted;font.pixelSize:11;elide:Text.ElideMiddle;text:editor.job.running?editor.job.status:editor.state.exportState==="done"?editor.state.exportPath:editor.state.exportMessage}
                }
                SmallButton{visible:editor.job.running;text:"Cancel";onClicked:editor.job.cancel()}
                SmallButton{visible:editor.state.exportState==="done";text:"Open folder";onClicked:files.revealFolder(editor.state.exportPath)}
                SmallButton{visible:editor.state.exportState==="error";text:"Details";onClicked:exportDetails.open()}
                SmallButton{visible:!editor.job.running;text:"×";onClicked:editor.dismissExport()}
            }
            ProgressBar{anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:3;visible:editor.job.running;value:editor.job.progress}
        }
    }
    // Native system move/resize keeps the translucent Windows surface usable.
    Repeater {
        model:Qt.platform.os!=="windows"?[]:[
            {edges:Qt.LeftEdge|Qt.TopEdge,x:0,y:0,w:8,h:8,cursor:Qt.SizeFDiagCursor},
            {edges:Qt.RightEdge|Qt.TopEdge,x:root.width-8,y:0,w:8,h:8,cursor:Qt.SizeBDiagCursor},
            {edges:Qt.LeftEdge|Qt.BottomEdge,x:0,y:root.height-8,w:8,h:8,cursor:Qt.SizeBDiagCursor},
            {edges:Qt.RightEdge|Qt.BottomEdge,x:root.width-8,y:root.height-8,w:8,h:8,cursor:Qt.SizeFDiagCursor},
            {edges:Qt.LeftEdge,x:0,y:8,w:5,h:root.height-16,cursor:Qt.SizeHorCursor},
            {edges:Qt.RightEdge,x:root.width-5,y:8,w:5,h:root.height-16,cursor:Qt.SizeHorCursor},
            {edges:Qt.TopEdge,x:8,y:0,w:root.width-16,h:5,cursor:Qt.SizeVerCursor},
            {edges:Qt.BottomEdge,x:8,y:root.height-5,w:root.width-16,h:5,cursor:Qt.SizeVerCursor}]
        MouseArea{required property var modelData;z:100;x:modelData.x;y:modelData.y;width:modelData.w;height:modelData.h;enabled:root.visibility!==Window.Maximized;cursorShape:modelData.cursor;onPressed:root.startSystemResize(modelData.edges)}
    }
    property bool typing:activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)
    property bool modal:settings.opened||exportSheet.opened||warning.opened||paletteSheet.opened||extractionPopupOpen||openFile.visible||outputName.opened||replaceOutput.opened||profileFile.visible||exportDetails.opened
    Instantiator{model:editor.commands;delegate:Shortcut{required property var modelData;sequence:modelData.key;enabled:!root.modal&&!root.typing&&!(root.activeFocusItem&&(root.activeFocusItem.objectName==="editHandle"||root.activeFocusItem.objectName==="deleteRegion"||root.activeFocusItem.objectName==="muteRegion"||root.activeFocusItem.objectName==="muteHandle")&&(modelData.id==="back"||modelData.id==="forward"||modelData.id==="jumpBack"||modelData.id==="jumpForward"||modelData.id==="removeDelete"));onActivated:editor.dispatch(modelData.id)}}
    Connections {
        target:editor
        function onRequest(action){if(action==="open")root.requestOpen();else if(action==="settings")settings.open();else if(action==="export"){extractTrack=-1;exportSheet.open()}else if(action==="palette")paletteSheet.open();else if(action==="morePreview")root.resizePreview(24);else if(action==="moreAudio")root.resizePreview(-24);else if(action==="zoomIn")root.zoom(.5);else if(action==="zoomOut")root.zoom(2);else if(action==="fit"){viewStart=0;viewEnd=Math.max(1,editor.state.duration)}}
        function onNotice(text){root.message=text;messageTimer.restart()}
        function onChanged(){if(editor.state.loaded&&(root.loadedPath!==editor.state.path||viewEnd>editor.state.duration)){root.loadedPath=editor.state.path;viewStart=0;viewEnd=editor.state.duration}}
    }
    Timer{id:messageTimer;interval:12000;onTriggered:root.message=""}
    FileBrowser{id:openFile;objectName:"openBrowser";title:"Open recording";nameFilters:["Video (*.mkv *.mp4 *.mov *.webm *.avi *.m4v)","All files (*)"];onAccepted:{editor.open(selectedFile);viewStart=0;viewEnd=1}}
    ThemedDialog {
        id:outputName;objectName:"outputNameDialog";title:"Export name";modal:true;anchors.centerIn:parent;width:Math.min(460,root.width-40)
        property string error:""
        property string defaultName:files.name(editor.suggestedOutput(root.outputOptions.compress||false,root.outputOptions.extractTrack===undefined?-1:root.outputOptions.extractTrack,root.outputOptions.original||false))
        onOpened:{error="";exportName.clear();Qt.callLater(()=>exportName.forceActiveFocus())}
        ColumnLayout {
            width:parent.width;spacing:8
            CompactTextField{id:exportName;objectName:"exportName";Layout.fillWidth:true;placeholderText:outputName.defaultName;palette.placeholderText:appTheme.colors.muted;selectByMouse:true;onAccepted:root.saveNamedOutput()}
            Label{Layout.fillWidth:true;text:"Saves beside the imported clip. Enter uses the default name.";font.pixelSize:11;color:appTheme.colors.muted;wrapMode:Text.Wrap}
            Label{Layout.fillWidth:true;text:files.path(files.parent(editor.suggestedOutput()));color:appTheme.colors.muted;font.pixelSize:11;elide:Text.ElideMiddle}
            Label{visible:!!outputName.error;Layout.fillWidth:true;text:outputName.error;color:appTheme.colors.focus;wrapMode:Text.Wrap}
            RowLayout{Item{Layout.fillWidth:true}SmallButton{text:"Cancel";onClicked:outputName.close()}SmallButton{primary:true;text:"Export";onClicked:root.saveNamedOutput()}}
        }
    }
    ThemedDialog {
        id:replaceOutput;objectName:"replaceOutputDialog";title:"Replace existing file?";modal:true;anchors.centerIn:parent;width:350;standardButtons:Dialog.Yes|Dialog.No
        Label{width:parent.width;text:files.name(root.proposedOutput);wrapMode:Text.Wrap;color:appTheme.colors.muted}
        onAccepted:root.commitOutput()
    }
    ThemedDialog {
        id:exportDetails;title:"Export error";modal:true;anchors.centerIn:parent;width:Math.min(600,root.width-40);height:Math.min(350,root.height-40);standardButtons:Dialog.Close
        ScrollView{anchors.fill:parent;clip:true;TextArea{readOnly:true;wrapMode:Text.Wrap;text:editor.state.exportMessage;selectByMouse:true}}
    }
    ThemedDialog {
        id:warning;width:350;title:editor.job.running?"Export is running":"Discard unfinished edits?";modal:true;anchors.centerIn:parent;standardButtons:Dialog.Discard|Dialog.Cancel
        Label{text:editor.job.running?"Leaving cancels the current export.":"Your edits exist only for this session.";color:appTheme.colors.muted}
        onDiscarded:{if(pendingAction==="close"){editor.job.cancel();allowClose=true;root.close()}else openFile.open()}
    }
    ThemedDialog {
        id:exportSheet;title:"Export";modal:true;anchors.centerIn:parent;width:440
        function values(){return {compress:compress.checked,targetMB:Number(target.text),scale:scale.currentValue,reduceFps:fps.checked,overwrite:true}}
        property var summary:{const state=editor.state;const policy=editor.fastCopy;const profile=editor.profile;const force=editor.forceEncode;return editor.exportSummary(values())}
        ColumnLayout {
            width:parent.width;spacing:8
            CompactCheckBox{text:"Collapse selected audio into one track";checked:editor.state.collapse;onClicked:editor.collapse(checked)}
            CompactCheckBox{id:compress;text:"Compress and re-encode"}
            GridLayout{visible:compress.checked;columns:2;columnSpacing:12
                Label{text:"Target MB"}CompactTextField{id:target;objectName:"compressTarget";text:"20";Layout.preferredWidth:100;validator:DoubleValidator{bottom:1;top:100000}}
                Label{text:"Dimensions"}CompactComboBox{id:scale;textRole:"label";valueRole:"value";model:[{label:"100%",value:1},{label:"75%",value:.75},{label:"50%",value:.5},{label:"25%",value:.25}]}
                CompactCheckBox{id:fps;text:"Reduce above 30 fps to 30";checked:true;Layout.columnSpan:2}
            }
            Label{Layout.fillWidth:true;text:exportSheet.summary.error||exportSheet.summary.summary;color:exportSheet.summary.error?appTheme.colors.focus:appTheme.colors.text;wrapMode:Text.Wrap}
            Label{Layout.fillWidth:true;text:"Final duration "+root.clock(exportSheet.summary.duration)+(compress.checked?" · Video "+exportSheet.summary.videoKbps+" kb/s":"");color:appTheme.colors.muted;wrapMode:Text.Wrap}
            Label{visible:editor.fastCopy;Layout.fillWidth:true;text:"Effective copy ranges: "+exportSheet.summary.ranges;color:appTheme.colors.muted;font.pixelSize:11;wrapMode:Text.Wrap}
            Label{text:"Profile: "+(compress.checked?"CPU · Two pass":editor.profile);color:appTheme.colors.muted}
            RowLayout{Item{Layout.fillWidth:true}SmallButton{text:"Cancel";onClicked:exportSheet.close()}SmallButton{primary:true;text:"Export…";enabled:!exportSheet.summary.error&&target.acceptableInput;onClicked:root.beginOutput(exportSheet.values())}}
        }
    }
    ThemedDialog {
        id:settings;objectName:"settingsDialog";title:"Settings";modal:true;anchors.centerIn:parent;width:Math.min(600,root.width-40)
        height:Math.min(root.height-40,topPadding+bottomPadding+(header?header.height:0)+tabs.implicitHeight+settingsFooter.implicitHeight+24+(tabs.currentIndex===1?390:tabs.currentIndex===2?diagnosticsPage.implicitHeight:tabs.currentIndex===3?appearancePage.implicitHeight:editingPage.implicitHeight))
        ColumnLayout {
            anchors.fill:parent;spacing:12
            TabBar {
                id:tabs;objectName:"settingsTabs";Layout.fillWidth:true;implicitHeight:28
                component SettingsTab:TabButton {
                    id:tab;implicitHeight:28;padding:4
                    contentItem:Text{text:tab.text;font:tab.font;color:tab.checked?appTheme.colors.text:appTheme.colors.muted;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter}
                    background:Rectangle {
                        color:tab.hovered?appTheme.colors.controlHover:appTheme.colors.panel;radius:4
                        border.color:tab.activeFocus?appTheme.colors.focus:"transparent"
                        Rectangle{anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom;height:2;color:tab.checked?appTheme.colors.accent:"transparent"}
                    }
                }
                SettingsTab{text:"Editing / export"}SettingsTab{text:"Keyboard"}SettingsTab{text:"Diagnostics"}SettingsTab{text:"Appearance"}
            }
            StackLayout {
                Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;currentIndex:tabs.currentIndex
                ColumnLayout {
                    id:editingPage;Layout.fillHeight:false;spacing:8
                    Label{text:"Cut policy"}
                    CompactComboBox{model:["Frame accurate","Fast copy (snap to keyframes)"];currentIndex:editor.fastCopy?1:0;onActivated:editor.fastCopy=currentIndex===1;Layout.fillWidth:true}
                    Label{Layout.fillWidth:true;text:"Frame accurate encodes when exact cuts require it. Fast copy shows snapped ranges before export.";wrapMode:Text.Wrap;color:appTheme.colors.muted}
                    Label{text:"Ordinary export profile"}
                    CompactComboBox{model:editor.profiles;textRole:"label";valueRole:"id";Layout.fillWidth:true;Component.onCompleted:currentIndex=Math.max(0,indexOfValue(editor.profile));onActivated:editor.profile=currentValue}
                    Label{Layout.fillWidth:true;text:"GPU profiles appear after a real encoder probe succeeds. Selecting a profile keeps copying eligible streams.";wrapMode:Text.Wrap;color:appTheme.colors.muted}
                    CompactCheckBox{text:"Always re-encode video";checked:editor.forceEncode;onClicked:editor.forceEncode=checked}
                }
                ColumnLayout {
                    id:keyboardPage;spacing:8
                    Label{text:"Edit a combination, then Enter. Empty disables it.";color:appTheme.colors.muted}
                    ScrollView{Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                        Column{width:parent.width;spacing:3;Repeater{model:editor.commands
                            RowLayout{required property var modelData;width:parent.width
                                Label{text:modelData.label;Layout.fillWidth:true}
                                CompactTextField{text:modelData.key;Layout.preferredWidth:150;onAccepted:{const error=editor.bind(modelData.id,text);if(error)root.message=error}}
                            }
                        }}
                    }
                    SmallButton{text:"Reset bindings";onClicked:editor.resetBindings()}
                }
                ColumnLayout {
                    id:diagnosticsPage;Layout.fillHeight:false;spacing:8
                    Label{Layout.fillWidth:true;text:preview.diagnostics||"No active preview";wrapMode:Text.Wrap;color:appTheme.colors.muted}
                    Label{visible:!!preview.error;Layout.fillWidth:true;text:preview.error;wrapMode:Text.Wrap;color:appTheme.colors.focus}
                    Label{Layout.fillWidth:true;text:"Profiler: "+(profiler.path||"off");wrapMode:Text.Wrap;color:appTheme.colors.muted}
                    SmallButton{text:profiler.path?"Stop profiling":"Start performance log…";onClicked:{if(profiler.path)profiler.setPath("");else profileFile.open()}}
                    Label{Layout.fillWidth:true;text:"Logs: seeks, delivered video buffers, sink statistics, UI frame intervals, memory, encoder probes, export stages.";wrapMode:Text.Wrap;color:appTheme.colors.muted}
                }
                ScrollView {
                    id:appearanceScroll;Layout.minimumHeight:0;contentWidth:availableWidth;contentHeight:appearancePage.implicitHeight;clip:true
                    ColumnLayout {
                    id:appearancePage;width:appearanceScroll.availableWidth;spacing:8
                    Label{text:"Theme"}
                    CompactComboBox {
                        id:themeChoice;objectName:"themeChoice";Layout.fillWidth:true
                        model:appTheme.presets;textRole:"label";valueRole:"id"
                        currentIndex:Math.max(0,indexOfValue(appTheme.preset))
                        onActivated:{appTheme.preset=currentValue;root.appearanceError=""}
                        Connections{target:appTheme;function onChanged(){themeChoice.currentIndex=Math.max(0,themeChoice.indexOfValue(appTheme.preset))}}
                    }
                    RowLayout {
                        Layout.fillWidth:true
                        Label{text:"Background opacity";Layout.preferredWidth:140}
                        Slider{objectName:"backgroundOpacity";Layout.fillWidth:true;from:0;to:1;stepSize:.01;value:appTheme.backgroundOpacity;onMoved:appTheme.backgroundOpacity=value}
                        Label{text:Math.round(appTheme.backgroundOpacity*100)+"%";Layout.preferredWidth:35;horizontalAlignment:Text.AlignRight}
                    }
                    RowLayout {
                        Layout.fillWidth:true
                        Label{text:"Dialog / menu opacity";Layout.preferredWidth:150}
                        Slider{objectName:"popupOpacity";Layout.fillWidth:true;from:0;to:1;stepSize:.01;value:appTheme.popupOpacity;onMoved:appTheme.popupOpacity=value}
                        Label{text:Math.round(appTheme.popupOpacity*100)+"%";Layout.preferredWidth:35;horizontalAlignment:Text.AlignRight}
                    }
                    Label{Layout.fillWidth:true;text:"Text, icons, waveforms and video stay opaque. Blur comes from your compositor.";color:appTheme.colors.muted;wrapMode:Text.Wrap}
                    RowLayout {
                        SmallButton{text:"Import…";onClicked:themeImport.open()}
                        SmallButton{text:"Save as…";onClicked:themeExport.open()}
                        SmallButton{text:"Reload";onClicked:root.appearanceError=appTheme.reload()}
                        SmallButton{text:"Reset opacity";onClicked:appTheme.resetOpacity()}
                    }
                    Label{Layout.fillWidth:true;text:appTheme.directory;font.pixelSize:10;color:appTheme.colors.muted;elide:Text.ElideMiddle}
                    Label{visible:!!root.appearanceError;Layout.fillWidth:true;text:root.appearanceError;color:appTheme.colors.focus;wrapMode:Text.Wrap}
                    }
                }
            }
            RowLayout{id:settingsFooter;Layout.fillWidth:true;Item{Layout.fillWidth:true}SmallButton{objectName:"settingsClose";text:"Close";onClicked:settings.close()}}
        }
    }
    FileBrowser{id:profileFile;objectName:"profileBrowser";title:"Performance log";saveMode:true;nameFilters:["JSON Lines (*.jsonl)"];onAccepted:profiler.setPath(selectedFile)}
    FileBrowser{id:themeImport;title:"Import theme";nameFilters:["ClipCut theme (*.json)"];onAccepted:root.appearanceError=appTheme.importTheme(selectedFile)}
    FileBrowser{id:themeExport;title:"Export theme";saveMode:true;selectedFile:files.resolve("clipcut-theme.json",files.initialFolder);nameFilters:["ClipCut theme (*.json)"];onAccepted:root.appearanceError=appTheme.exportTheme(selectedFile)}
    ThemedDialog {
        id:paletteSheet;title:"Commands";modal:true;anchors.centerIn:parent;width:430;height:Math.min(490,root.height-60);onOpened:commandSearch.forceActiveFocus()
        ColumnLayout{anchors.fill:parent
            CompactTextField{id:commandSearch;Layout.fillWidth:true;placeholderText:"Find a command…";onAccepted:{for(let c of editor.commands){if(c.label.toLowerCase().includes(text.toLowerCase())){paletteSheet.close();editor.dispatch(c.id);break}}}}
            ScrollView{Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                Column{width:parent.width;Repeater{model:editor.commands;SmallButton{required property var modelData;width:parent.width;visible:modelData.label.toLowerCase().includes(commandSearch.text.toLowerCase());text:modelData.label+"    "+modelData.key;onClicked:{paletteSheet.close();editor.dispatch(modelData.id)}}}}
            }
        }
    }
}
