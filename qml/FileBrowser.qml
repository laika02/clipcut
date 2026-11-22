import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel
import ClipCut 1.0

ThemedDialog {
    id:picker
    objectName:"fileBrowser"
    modal:true
    anchors.centerIn:parent
    width:Math.min(720,parent.width-40)
    height:Math.min(520,parent.height-40)
    padding:12
    palette.placeholderText:appTheme.colors.placeholder
    property bool saveMode:false
    property url selectedFile:""
    property url currentFolder:files.initialFolder
    property var nameFilters:["All files (*)"]
    property string error:""
    readonly property string filterText:search.text
    readonly property int resultCount:filtered.count
    property var completionMatches:[]
    property int completionIndex:-1
    property bool applyingCompletion:false
    function resetCompletion(){completionMatches=[];completionIndex=-1}
    function completeSearch(backward) {
        if(!search.text.length)return false
        if(!completionMatches.length) {
            const matches=[]
            for(let i=0;i<filtered.count;++i)matches.push(filtered.get(i,"fileName"))
            if(!matches.length)return false
            // Keep the original matches as completing a name narrows the live filter.
            completionMatches=matches
            completionIndex=backward?0:-1
        }
        completionIndex=(completionIndex+(backward?-1:1)+completionMatches.length)%completionMatches.length
        const name=completionMatches[completionIndex]
        applyingCompletion=true
        search.text=name
        applyingCompletion=false
        search.cursorPosition=name.length
        for(let i=0;i<filtered.count;++i) {
            if(filtered.get(i,"fileName")===name){listing.currentIndex=i;break}
        }
        return true
    }
    onCurrentFolderChanged:resetCompletion()
    function navigate(folder) {
        if(!files.isDirectory(folder)){error="Folder does not exist.";return}
        currentFolder=folder;search.clear();error="";listing.currentIndex=0
    }
    function focusSearch(){search.forceActiveFocus();search.selectAll()}
    function activate(index) {
        if(index<0||index>=filtered.count)return
        const target=filtered.get(index,"fileUrl")
        if(filtered.isFolder(index))navigate(target)
        else {selectedFile=target;filename.text=files.name(target);if(!saveMode)finish(target)}
    }
    function finish(target) {
        if(files.isDirectory(target)){navigate(target);return}
        if(!saveMode&&!files.exists(target)){error="Choose an existing file.";return}
        if(!files.isDirectory(files.parent(target))){error="The destination folder does not exist.";return}
        selectedFile=target
        if(saveMode&&files.exists(target)){replace.open();return}
        files.remember(currentFolder);accept()
    }
    function choose() {
        if(saveMode) {
            if(!filename.text.trim()){error="Enter a filename.";filename.forceActiveFocus();return}
            let name=filename.text.trim()
            const match=String(nameFilters[types.currentIndex]||"").match(/\*\.([a-zA-Z0-9]+)/)
            if(match&&!files.name(files.resolve(name,currentFolder)).includes("."))name+="."+match[1]
            finish(files.resolve(name,currentFolder))
        } else activate(listing.currentIndex)
    }
    onOpened:{
        error="";search.clear();types.currentIndex=0
        if(String(selectedFile)){currentFolder=files.parent(selectedFile);filename.text=saveMode?files.name(selectedFile):""}
        listing.currentIndex=0
        Qt.callLater(()=>listing.forceActiveFocus())
    }
    Shortcut{sequence:"/";enabled:picker.opened&&picker.contentItem.Window.window&&!(picker.contentItem.Window.window.activeFocusItem instanceof TextInput)&&!(picker.contentItem.Window.window.activeFocusItem instanceof TextEdit)&&!replace.opened;onActivated:picker.focusSearch()}
    Shortcut{sequence:"Ctrl+F";enabled:picker.opened&&!replace.opened;onActivated:picker.focusSearch()}
    Shortcut{sequence:"Ctrl+L";enabled:picker.opened&&!replace.opened;onActivated:{location.forceActiveFocus();location.selectAll()}}
    Shortcut{sequence:"Alt+Up";enabled:picker.opened&&!replace.opened;onActivated:picker.navigate(entries.parentFolder)}
    FolderListModel{
        id:entries;folder:picker.currentFolder
        showDirsFirst:true;showDotAndDotDot:false
        caseSensitive:false;sortCaseSensitive:false
        onStatusChanged:{
            if(status===FolderListModel.Loading)picker.resetCompletion()
            if(status===FolderListModel.Ready)listing.currentIndex=filtered.count?0:-1
        }
    }
    BrowserFilter{
        id:filtered;sourceModel:entries;query:search.text
        patterns:{
            const type=String(picker.nameFilters[types.currentIndex]||"All files (*)")
            return type.substring(type.lastIndexOf("(")+1,type.lastIndexOf(")")).match(/\*[^\s)]*/g)||["*"]
        }
        onCountChanged:listing.currentIndex=count?0:-1
        onPatternsChanged:picker.resetCompletion()
    }
    ColumnLayout{
        anchors.fill:parent;spacing:7
        RowLayout{
            Layout.fillWidth:true
            SmallButton{text:"↑";implicitWidth:28;implicitHeight:28;onClicked:picker.navigate(entries.parentFolder);ToolTip.text:"Parent folder · Alt+Up";ToolTip.visible:hovered}
            CompactTextField{id:location;Layout.fillWidth:true;implicitHeight:28;text:files.path(picker.currentFolder);selectByMouse:true;onAccepted:picker.navigate(files.resolve(text,picker.currentFolder))}
        }
        RowLayout{
            CompactTextField{
                id:search;objectName:"fileSearch";Layout.fillWidth:true;implicitHeight:28
                placeholderText:"Filter filenames…  /";selectByMouse:true
                onTextChanged:if(!picker.applyingCompletion)picker.resetCompletion()
                onActiveFocusChanged:if(!activeFocus)picker.resetCompletion()
                Keys.onTabPressed:event=>{event.accepted=picker.completeSearch(false)}
                Keys.onBacktabPressed:event=>{event.accepted=picker.completeSearch(true)}
                Keys.onDownPressed:{listing.forceActiveFocus();if(listing.currentIndex<0&&filtered.count)listing.currentIndex=0}
                Keys.onEscapePressed:event=>{if(text)clear();else picker.reject();event.accepted=true}
                onAccepted:{listing.forceActiveFocus();picker.choose()}
            }
            CompactComboBox{id:types;objectName:"fileTypes";model:picker.nameFilters;Layout.preferredWidth:180;implicitHeight:28}
        }
        RowLayout{
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:10
            ColumnLayout{
                Layout.minimumWidth:100;Layout.preferredWidth:100;Layout.maximumWidth:100;Layout.alignment:Qt.AlignTop;spacing:3
                Repeater{model:files.places;SmallButton{required property var modelData;text:modelData.label;Layout.fillWidth:true;implicitHeight:28;onClicked:picker.navigate(modelData.url)}}
            }
            Rectangle{
                Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumWidth:120;color:appTheme.backgrounds.panel;radius:3;border.color:appTheme.colors.border
                ListView{
                    id:listing;objectName:"fileListing";anchors.fill:parent;anchors.margins:2;clip:true;model:filtered
                    keyNavigationEnabled:true;keyNavigationWraps:false;activeFocusOnTab:true
                    ScrollBar.vertical:ScrollBar{}
                    Keys.onReturnPressed:picker.activate(currentIndex)
                    Keys.onEnterPressed:picker.activate(currentIndex)
                    delegate:ItemDelegate{
                        id:entry
                        required property int index
                        required property string fileName
                        required property bool fileIsDir
                        width:listing.width;height:28
                        highlighted:listing.currentIndex===index
                        contentItem:Text{text:(entry.fileIsDir?"▸  ":"    ")+entry.fileName;font:entry.font;color:appTheme.colors.text;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
                        background:Rectangle{radius:3;color:entry.highlighted?appTheme.colors.selection:entry.hovered?appTheme.colors.controlHover:"transparent";border.width:entry.activeFocus?1:0;border.color:appTheme.colors.focus}
                        onClicked:{listing.currentIndex=index;listing.forceActiveFocus();if(!fileIsDir){picker.selectedFile=filtered.get(index,"fileUrl");filename.text=fileName}}
                        onDoubleClicked:picker.activate(index)
                    }
                }
                Label{anchors.centerIn:parent;text:entries.status===FolderListModel.Loading?"Loading…":"No matching files";visible:entries.status===FolderListModel.Loading||filtered.count===0;color:appTheme.colors.muted}
            }
        }
        CompactTextField{id:filename;objectName:"fileName";visible:picker.saveMode;Layout.fillWidth:true;implicitHeight:28;placeholderText:"Filename";selectByMouse:true;onAccepted:picker.choose()}
        RowLayout{
            Layout.fillWidth:true
            Label{Layout.fillWidth:true;text:picker.error||"/ filter · Tab complete · Ctrl+L location · Enter choose";elide:Text.ElideRight;color:picker.error?appTheme.colors.focus:appTheme.colors.muted;font.pixelSize:11}
            SmallButton{text:"Cancel";implicitHeight:28;onClicked:picker.reject()}
            SmallButton{primary:true;text:picker.saveMode?"Save":"Open";implicitHeight:28;enabled:picker.saveMode||listing.currentIndex>=0;onClicked:picker.choose()}
        }
    }
    ThemedDialog{
        id:replace;objectName:"replaceFile";modal:true;anchors.centerIn:parent;width:Math.min(360,picker.width-30)
        title:"Replace existing file?";standardButtons:Dialog.Yes|Dialog.No
        Label{width:parent.width;text:files.name(picker.selectedFile);wrapMode:Text.Wrap;color:appTheme.colors.muted}
        onAccepted:{files.remember(picker.currentFolder);picker.accept()}
    }
}
