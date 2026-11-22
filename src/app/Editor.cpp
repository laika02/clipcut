#include "Editor.hpp"
#include <QtConcurrent>
#include <QFileInfo>
#include <QKeySequence>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>
using namespace clipcut;
struct Command { const char *id, *label, *key; };
static const Command commandList[] = {
    {"open","Open recording","Ctrl+O"},{"export","Export","Ctrl+E"},{"settings","Settings","Ctrl+,"},
    {"morePreview","Enlarge preview","Alt+Shift+Up"},{"moreAudio","Enlarge audio area","Alt+Shift+Down"},
    {"palette","Command palette","Ctrl+K"},{"play","Play / pause","Space"},
    {"back","Previous frame","Left"},{"forward","Next frame","Right"},
    {"jumpBack","Back five seconds","Shift+Left"},{"jumpForward","Forward five seconds","Shift+Right"},
    {"in","Set trim start here","I"},{"out","Set trim end here","O"},
    {"deleteStart","Mark ripple-delete start","["},{"deleteEnd","Mark ripple-delete end","]"},
    {"addDelete","Add ripple delete here","Ctrl+D"},{"removeDelete","Remove selected delete or mute","Delete"},
    {"addMute","Add mute on active audio track","Ctrl+Shift+M"},
    {"muteStart","Mark track mute start","Ctrl+["},{"muteEnd","Mark track mute end","Ctrl+]"},
    {"undo","Undo","Ctrl+Z"},{"redo","Redo","Ctrl+Shift+Z"},
    {"resetCrop","Reset crop","Ctrl+R"},{"resetTrim","Reset trim and deletes","Ctrl+Shift+R"},
    {"zoomIn","Zoom timeline in","="},{"zoomOut","Zoom timeline out","-"},{"fit","Fit timeline","F"},
    {"mix","Preview included audio","M"}
    ,{"previousDelete","Select previous delete","Alt+Left"},{"nextDelete","Select next delete","Alt+Right"}
};
Editor::Editor(Preview *preview) : m_preview(preview) {
    QCoreApplication::instance()->installEventFilter(this);
    connect(&m_job,&ExportJob::logEvent,this,&Editor::logEvent);
    connect(&m_job,&ExportJob::finished,this,[this](bool ok,const QString &path,const QString &message) {
        emit logEvent("export_finished",{{"success",ok},{"path",path},{"message",message}});
        m_exportState=ok?"done":message=="Cancelled"?"cancelled":"error";
        m_exportPath=path;m_exportMessage=message;
        if(ok && m_exportRevision == m_revision) m_dirty = false;
        emit changed();
    });
    m_profiles = {QVariantMap{{"id","cpu-fast"},{"label","CPU · Fast"}}, QVariantMap{{"id","cpu-balanced"},{"label","CPU · Balanced"}}};
    m_tasks.append(QtConcurrent::run([this] {
        for(const auto &candidate : {QString("nvenc"),QString("qsv"),QString("vaapi")}) {
#ifdef Q_OS_WIN
            if(candidate == "vaapi") continue;
#endif
            QStringList args{"-v","error"};
            if(candidate == "vaapi") args += {"-vaapi_device","/dev/dri/renderD128"};
            args += {"-f","lavfi","-i","color=size=128x128:rate=30","-frames:v","2"};
            if(candidate == "vaapi") args += {"-vf","format=nv12,hwupload"};
            args += {"-c:v",candidate == "nvenc" ? "h264_nvenc" : candidate == "qsv" ? "h264_qsv" : "h264_vaapi","-f","null","-"};
            bool ok = false; QString reason;
            try { runTool(mediaTool("ffmpeg"),args,nullptr,12000); ok = true; }
            catch(const std::exception &e) { reason = QString::fromUtf8(e.what()); }
            QMetaObject::invokeMethod(this,[this,candidate,ok,reason] {
                emit logEvent("encoder_probe",{{"encoder",candidate},{"available",ok},{"reason",reason}});
                if(ok) { m_profiles.append(QVariantMap{{"id",candidate},{"label","GPU · " + candidate.toUpper()}}); emit profilesChanged(); }
            });
        }
    }));
}
Editor::~Editor() { if(m_cancel) *m_cancel = true; for(auto &task:m_tasks) task.waitForFinished(); }
void Editor::setProfile(const QString &v) { m_settings.setValue("profile",v); emit preferencesChanged(); }
void Editor::setFastCopy(bool v) { m_settings.setValue("fastCopy",v); emit preferencesChanged(); }
void Editor::setForceEncode(bool v) { m_settings.setValue("forceEncode",v); emit preferencesChanged(); }
QVariantMap Editor::state() const {
    QVariantList tracks, deletes;
    for(int i=0;i<m_source.audio.size();++i) {
        QVariantList mutes;
        const auto intervals=m_edits.mutes.value(i);
        for(int j=0;j<intervals.size();++j)mutes.append(QVariantMap{{"index",j},{"start",seconds(intervals[j].start)},{"end",seconds(intervals[j].end)},{"selected",m_selectedMuteTrack==i&&m_selectedMute==j}});
        const auto a=m_source.audio[i]; tracks.append(QVariantMap{{"index",i},{"title",a.title.isEmpty()?QString("Track %1").arg(i+1):a.title},
            {"codec",a.codec},{"channels",a.channels},{"included",m_edits.included.contains(i)},{"solo",m_solo==i},{"wave",m_waves.value(i)},
            {"mutes",mutes},{"modified",temporalEdits(m_source,m_edits)||!intervals.isEmpty()},{"active",m_activeTrack==i}});
    }
    for(int i=0;i<m_edits.deletes.size();++i) { auto r=m_edits.deletes[i]; deletes.append(QVariantMap{{"index",i},{"start",seconds(r.start)},{"end",seconds(r.end)},{"selected",i==m_selectedDelete}}); }
    const auto c=m_edits.crop;
    return {{"path",m_source.path},{"name",QFileInfo(m_source.path).fileName()},{"loaded",m_source.duration>0},{"loading",m_loading},
        {"duration",seconds(m_source.duration)},{"fps",m_source.fps()},{"width",m_source.width},{"height",m_source.height},
        {"sar",double(m_source.sarNum)/std::max(1,m_source.sarDen)},{"rotation",m_source.rotation},
        {"cropX",c.x()},{"cropY",c.y()},{"cropW",c.width()},{"cropH",c.height()},
        {"trimStart",seconds(m_edits.trim.start)},{"trimEnd",seconds(m_edits.trim.end)},
        {"keptDuration",seconds(keptDuration(m_edits))},{"deletes",deletes},{"tracks",tracks},{"collapse",m_edits.collapse},
        {"dirty",m_dirty},{"modified",temporalEdits(m_source,m_edits)},{"canUndo",m_history.canUndo()},{"canRedo",m_history.canRedo()},
        {"keyframesReady",!m_keys.isEmpty()},{"frameIndexReady",!m_frames.isEmpty()},
        {"exportState",m_exportState},{"exportPath",m_exportPath},{"exportMessage",m_exportMessage}};
}
void Editor::open(const QUrl &url) {
    if(m_job.running()) { emit notice("Finish or cancel the export before opening another recording."); return; }
    const auto path=url.isLocalFile()?url.toLocalFile():url.toString();
    if(m_cancel) *m_cancel=true;
    m_cancel=std::make_shared<std::atomic_bool>(false); auto cancel=m_cancel; int generation=++m_generation;
    m_preview->play(false); m_loading=true; emit changed();
    m_tasks.append(QtConcurrent::run([this,path,cancel,generation] {
        try {
            auto source=probeSource(path);
            if(*cancel) return;
            QMetaObject::invokeMethod(this,[this,source,cancel,generation] {
                if(*cancel || generation!=m_generation) return;
                m_source=source; m_edits=initialEdits(source); m_history.reset(m_edits); m_waves.clear(); m_keys.clear(); m_frames.clear();
                m_solo=-1; m_selectedDelete=-1;m_activeTrack=source.audio.isEmpty()?-1:0;m_selectedMuteTrack=-1;m_selectedMute=-1; m_dirty=false; m_loading=false; ++m_revision;
                m_exportState.clear();m_exportPath.clear();m_exportMessage.clear();
                QVariantList ids;for(auto a:source.audio)ids.append(a.containerId);
                m_preview->open(source.path,ids); update(false); emit logEvent("media_open",{{"duration",seconds(source.duration)},{"audio_tracks",source.audio.size()}});
                analyze(source,cancel,generation);
            });
        } catch(const std::exception &e) {
            const auto error=QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this,[this,error,generation] { if(generation==m_generation) {m_loading=false;emit changed();emit notice(error);} });
        }
    }));
}
void Editor::update(bool commit) {
    if(commit&&!m_gesture) {
        m_selectedDelete=mergeRegions(m_edits.deletes,m_selectedDelete);
        for(auto it=m_edits.mutes.begin();it!=m_edits.mutes.end();++it) {
            const auto selected=mergeRegions(it.value(),it.key()==m_selectedMuteTrack?m_selectedMute:-1);
            if(it.key()==m_selectedMuteTrack)m_selectedMute=selected;
        }
        if(m_selectedMute<0)m_selectedMuteTrack=-1;
    }
    if(commit && !m_gesture && m_history.commit(m_edits)) {m_dirty=true;++m_revision;}
    QVariantList ranges, included;
    for(auto r:kept(m_edits)) ranges.append(QVariantMap{{"start",seconds(r.start)},{"end",seconds(r.end)}});
    for(auto i:m_edits.included) included.append(i);
    m_preview->ranges(ranges); m_preview->route(m_solo,included);m_preview->mutes(m_edits.mutes); emit changed();
}
bool Editor::eventFilter(QObject *watched,QEvent *event) {
    if(muteDragging()&&watched==m_muteDragWindow) {
        if(event->type()==QEvent::MouseMove) {
            moveMuteDrag(static_cast<QMouseEvent*>(event)->position().x());event->accept();return true;
        }
        if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
            moveMuteDrag(static_cast<QMouseEvent*>(event)->position().x());finishMuteDrag();
            // Let Quick clear its pressed state, after the window-owned edit
            // has finished. No local MouseArea can end the transaction early.
            return false;
        }
        if((event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease)&&static_cast<QMouseEvent*>(event)->button()==Qt::RightButton){event->accept();return true;}
        if(event->type()==QEvent::Hide||event->type()==QEvent::Close||event->type()==QEvent::WindowDeactivate)finishMuteDrag();
    }
    // Touchpads deliver scrolling separately from their held mouse button.
    // Keep scrolling/gesture handlers out of an active edit transaction even
    // when the pointer has left its MouseArea or entered another audio row.
    if(m_gesture&&(event->type()==QEvent::Wheel||event->type()==QEvent::NativeGesture)) {
        ++m_blockedGestureScrolls;event->accept();return true;
    }
    return QObject::eventFilter(watched,event);
}
void Editor::beginGesture() {if(!m_gesture)m_blockedGestureScrolls=0;m_gesture=true; }
void Editor::endGesture() {
    if(!m_gesture||muteDragging())return;m_gesture=false;
    if(m_blockedGestureScrolls)emit logEvent("edit_gesture",{{"blocked_scroll_events",m_blockedGestureScrolls}});
    update();
}
void Editor::beginMuteDrag(int track,int index,int part,QQuickItem *timeline,double x) {
    if(muteDragging()||!timeline||!timeline->window()||timeline->width()<=0||index<0||index>=m_edits.mutes.value(track).size())return;
    m_muteDragTrack=track;m_muteDragIndex=index;m_muteDragPart=part;m_muteDragWindow=timeline->window();
    m_muteDragRange=m_edits.mutes.value(track)[index];
    m_muteDragPressX=timeline->mapToScene(QPointF(x,0)).x();
    m_muteDragSecondsPerPixel=(timeline->property("viewEnd").toDouble()-timeline->property("viewStart").toDouble())/timeline->width();
    selectMute(track,index);beginGesture();emit muteDragChanged();
    emit logEvent("mute_drag_start",{{"track",track},{"part",part},{"start",seconds(m_muteDragRange.start)},{"end",seconds(m_muteDragRange.end)}});
}
void Editor::moveMuteDrag(double sceneX) {
    if(!muteDragging())return;
    const auto delta=(sceneX-m_muteDragPressX)*m_muteDragSecondsPerPixel;
    const auto start=seconds(m_muteDragRange.start),end=seconds(m_muteDragRange.end),minimum=1/std::max(1.,m_source.fps());
    if(m_muteDragPart==0)setMute(m_muteDragTrack,m_muteDragIndex,std::min(start+delta,end-minimum),end);
    else if(m_muteDragPart==1)setMute(m_muteDragTrack,m_muteDragIndex,start,std::max(end+delta,start+minimum));
    else moveMute(m_muteDragTrack,m_muteDragIndex,start+delta);
}
void Editor::finishMuteDrag() {
    if(!muteDragging())return;
    const auto track=m_muteDragTrack,part=m_muteDragPart;
    m_muteDragTrack=-1;m_muteDragWindow=nullptr;emit muteDragChanged();endGesture();
    emit logEvent("mute_drag_finish",{{"track",track},{"part",part}});emit muteDragFinished(track,part);
}
void Editor::noteMuteGrabLoss() {if(muteDragging())emit logEvent("mute_drag_local_grab_lost",{{"track",m_muteDragTrack},{"part",m_muteDragPart}});}
void Editor::trim(double start,double end) {
    auto minimum=fromSeconds(1/std::max(1.0,m_source.fps()));
    m_edits.trim={std::clamp(fromSeconds(start),Time(0),std::max(Time(0),m_source.duration-minimum)),std::clamp(fromSeconds(end),minimum,m_source.duration)};
    if(m_edits.trim.end<=m_edits.trim.start) m_edits.trim.end=std::min(m_source.duration,m_edits.trim.start+minimum);
    update();
}
void Editor::crop(double x,double y,double w,double h) { m_edits.crop=constrainedCrop(QRect(qRound(x),qRound(y),qRound(w),qRound(h)),{m_source.width,m_source.height});update(); }
void Editor::setDelete(int index,double start,double end) {
    if(index<0||index>=m_edits.deletes.size())return;
    auto s=std::clamp(fromSeconds(start),Time(0),m_source.duration),e=std::clamp(fromSeconds(end),Time(0),m_source.duration);
    m_edits.deletes[index]={std::min(s,e),std::max(s,e)};update();
}
int Editor::addDelete(double start,double end) {
    const auto minimum=std::min(m_source.duration,fromSeconds(1/std::max(1.,m_source.fps())));
    if(minimum<=0)return -1;
    const auto a=std::clamp(fromSeconds(std::min(start,end)),Time(0),m_source.duration-minimum);
    const auto b=std::clamp(fromSeconds(std::max(start,end)),a+minimum,m_source.duration);
    m_edits.deletes.append({a,b});m_selectedDelete=m_edits.deletes.size()-1;m_selectedMuteTrack=-1;m_selectedMute=-1;update();return m_selectedDelete;
}
void Editor::moveDelete(int index,double start) {
    if(index<0||index>=m_edits.deletes.size())return;
    const auto length=m_edits.deletes[index].end-m_edits.deletes[index].start;
    const auto at=std::clamp(fromSeconds(start),Time(0),std::max(Time(0),m_source.duration-length));
    m_edits.deletes[index]={at,at+length};update();
}
void Editor::selectDelete(int index) {m_selectedDelete=index;m_selectedMuteTrack=-1;m_selectedMute=-1;emit changed();}
void Editor::activateTrack(int track) {if(track>=0&&track<m_source.audio.size()&&m_activeTrack!=track){m_activeTrack=track;emit changed();}}
int Editor::addMute(int track,double start,double end) {
    if(track<0||track>=m_source.audio.size()||m_source.duration<=0)return -1;
    const auto minimum=std::min(m_source.duration,fromSeconds(1/std::max(1.,m_source.fps())));
    const auto a=std::clamp(fromSeconds(std::min(start,end)),Time(0),m_source.duration-minimum);
    const auto b=std::clamp(fromSeconds(std::max(start,end)),a+minimum,m_source.duration);
    auto &regions=m_edits.mutes[track];regions.append({a,b});
    m_activeTrack=track;m_selectedMuteTrack=track;m_selectedMute=regions.size()-1;m_selectedDelete=-1;update();return m_selectedMute;
}
void Editor::setMute(int track,int index,double start,double end) {
    if(!m_edits.mutes.contains(track)||index<0||index>=m_edits.mutes[track].size())return;
    const auto minimum=std::min(m_source.duration,fromSeconds(1/std::max(1.,m_source.fps())));
    const auto a=std::clamp(fromSeconds(std::min(start,end)),Time(0),m_source.duration-minimum);
    const auto b=std::clamp(fromSeconds(std::max(start,end)),a+minimum,m_source.duration);
    m_edits.mutes[track][index]={a,b};update();
}
void Editor::moveMute(int track,int index,double start) {
    if(!m_edits.mutes.contains(track)||index<0||index>=m_edits.mutes[track].size())return;
    const auto r=m_edits.mutes[track][index];const auto length=r.end-r.start;
    const auto at=std::clamp(fromSeconds(start),Time(0),std::max(Time(0),m_source.duration-length));
    m_edits.mutes[track][index]={at,at+length};update();
}
void Editor::selectMute(int track,int index) {m_activeTrack=track;m_selectedMuteTrack=track;m_selectedMute=index;m_selectedDelete=-1;emit changed();}
void Editor::removeMute(int track,int index) {
    if(muteDragging()) {
        const bool selected=track==m_muteDragTrack&&index==m_muteDragIndex;
        finishMuteDrag();if(selected){track=m_selectedMuteTrack;index=m_selectedMute;}
    }
    if(!m_edits.mutes.contains(track)||index<0||index>=m_edits.mutes[track].size())return;
    m_edits.mutes[track].removeAt(index);if(m_edits.mutes[track].isEmpty())m_edits.mutes.remove(track);
    m_selectedMuteTrack=-1;m_selectedMute=-1;update();
}
void Editor::include(int i) { if(m_edits.included.contains(i))m_edits.included.removeAll(i);else m_edits.included.append(i);std::sort(m_edits.included.begin(),m_edits.included.end());update(); }
void Editor::solo(int i,bool play) {m_activeTrack=i;m_solo=m_solo==i?-1:i;update(false);if(play)m_preview->play(true);}
void Editor::scrub(double time,int track,bool settle) {
    if(!m_scrubbing) {m_resume=m_preview->playing();m_preview->play(false);m_scrubbing=true;}
    if(track>=0){m_solo=track;m_activeTrack=track;}
    m_preview->route(-1,{}); m_preview->seek(std::clamp(time,0.0,seconds(m_source.duration)),settle);
    if(settle){m_scrubbing=false;update(false);if(m_resume)m_preview->play(true);}
}
void Editor::collapse(bool value){m_edits.collapse=value;update();}
QVariantList Editor::commands() const {
    QVariantList result;for(auto c:commandList)result.append(QVariantMap{{"id",c.id},{"label",c.label},{"key",m_settings.value(QString("keys/")+c.id,c.key)}});return result;
}
QString Editor::bind(const QString &id,const QString &sequence) {
    const auto key=QKeySequence::fromString(sequence,QKeySequence::PortableText);
    if(!sequence.isEmpty() && (key.isEmpty()||key.count()>1))return "Use one valid key combination, for example Ctrl+Shift+E.";
    const auto text=key.toString(QKeySequence::PortableText);
    if(text=="Tab"||text=="Shift+Tab"||text=="Esc"||text=="Return"||text=="Enter")return "That key is reserved for focus and dialog navigation.";
    for(int i=1;i<=9;++i)if(text==QString::number(i)||text=="Ctrl+"+QString::number(i))return "That combination is reserved for audio track selection.";
    for(auto c:commands()) {auto v=c.toMap();if(v["id"]!=id && !key.isEmpty() && QKeySequence(v["key"].toString())==key)return "Already assigned to " +v["label"].toString();}
    m_settings.setValue("keys/"+id,key.toString(QKeySequence::PortableText));emit bindingsChanged();return {};
}
void Editor::resetBindings(){m_settings.remove("keys");emit bindingsChanged();}
void Editor::dispatch(const QString &id) {
    if(muteDragging())finishMuteDrag();
    if(m_gesture)endGesture();
    if(id=="open"||id=="export"||id=="settings"||id=="palette"||id=="zoomIn"||id=="zoomOut"||id=="fit"||id=="morePreview"||id=="moreAudio"){emit request(id);return;}
    if(m_source.duration<=0)return;
    auto t=fromSeconds(m_preview->position());
    if(id=="play")m_preview->toggle();
    else if(id=="back"||id=="forward") {
        m_preview->play(false); Time next=t;
        if(!m_frames.isEmpty()) {auto it=std::upper_bound(m_frames.begin(),m_frames.end(),t+1000000);if(id=="forward")next=it==m_frames.end()?m_frames.last():*it;else{it=std::lower_bound(m_frames.begin(),m_frames.end(),t-1000000);next=it==m_frames.begin()?m_frames.first():*(it-1);}}
        else next+=fromSeconds((id=="forward"?1:-1)/std::max(1.0,m_source.fps()));
        m_preview->seek(seconds(std::clamp(next,Time(0),m_source.duration)),true);
    } else if(id=="jumpBack"||id=="jumpForward")m_preview->seek(std::clamp(seconds(t)+(id=="jumpBack"?-5:5),0.0,seconds(m_source.duration)));
    else if(id=="in")trim(seconds(t),seconds(m_edits.trim.end));
    else if(id=="out")trim(seconds(m_edits.trim.start),seconds(t));
    else if(id=="undo"||id=="redo"){m_edits=id=="undo"?m_history.undo():m_history.redo();m_dirty=true;++m_revision;update(false);}
    else if(id=="resetCrop"){m_edits.crop={0,0,m_source.width,m_source.height};update();}
    else if(id=="resetTrim"){m_edits.trim={0,m_source.duration};m_edits.deletes.clear();m_selectedDelete=-1;update();}
    else if(id=="addDelete"||id=="deleteStart") {m_edits.deletes.append({t,std::min(m_source.duration,t+(id=="addDelete"?Second:Time(0)))});m_selectedDelete=m_edits.deletes.size()-1;m_selectedMuteTrack=-1;m_selectedMute=-1;update();}
    else if(id=="addMute"||id=="muteStart")addMute(m_activeTrack,seconds(t),seconds(std::min(m_source.duration,t+(id=="addMute"?Second:Time(0)))));
    else if(id=="muteEnd"&&m_selectedMuteTrack>=0&&m_selectedMute>=0&&m_selectedMute<m_edits.mutes.value(m_selectedMuteTrack).size())setMute(m_selectedMuteTrack,m_selectedMute,seconds(m_edits.mutes.value(m_selectedMuteTrack)[m_selectedMute].start),seconds(t));
    else if(id=="deleteEnd" && m_selectedDelete>=0&&m_selectedDelete<m_edits.deletes.size())setDelete(m_selectedDelete,seconds(m_edits.deletes[m_selectedDelete].start),seconds(t));
    else if(id=="removeDelete"&&m_selectedMuteTrack>=0)removeMute(m_selectedMuteTrack,m_selectedMute);
    else if(id=="removeDelete" && m_selectedDelete>=0 && m_selectedDelete<m_edits.deletes.size()){m_edits.deletes.removeAt(m_selectedDelete);m_selectedDelete=-1;update();}
    else if(id=="mix"){m_solo=-1;update(false);}
    else if((id=="nextDelete"||id=="previousDelete")&&!m_edits.deletes.isEmpty()){int count=m_edits.deletes.size();m_selectedDelete=(m_selectedDelete+(id=="nextDelete"?1:count-1)+count)%count;emit changed();}
}
ExportOptions Editor::options(const QVariantMap &v) const {
    ExportOptions o;o.profile=profile();o.fastCopy=fastCopy();o.forceEncode=forceEncode();o.compress=v.value("compress",false).toBool();
    o.targetMB=v.value("targetMB",20).toDouble();o.scale=v.value("scale",1).toDouble();o.reduceFps=v.value("reduceFps",true).toBool();
    o.extractTrack=v.value("extractTrack",-1).toInt();o.original=v.value("original",false).toBool();o.overwrite=v.value("overwrite",false).toBool();return o;
}
QVariantMap Editor::exportSummary(const QVariantMap &v) const {
    auto p=planExport(m_source,m_edits,options(v),m_keys);QStringList ranges;
    for(auto r:p.ranges)ranges<<QString("%1–%2 s").arg(seconds(r.start),0,'f',3).arg(seconds(r.end),0,'f',3);
    return {{"summary",p.summary},{"error",p.error},{"duration",seconds(p.duration)},{"ranges",ranges.join(" · ")},{"videoKbps",p.videoKbps}};
}
QUrl Editor::suggestedOutput(bool compress,int track,bool original) const {
    const QFileInfo input(m_source.path);
    const auto suffix=track>=0?QString("_track%1%2.%3").arg(track+1).arg(original?"":"_modified",original?"mka":"m4a"):(compress?QString("_compressed.mp4"):QString("_edited.mp4"));
    return QUrl::fromLocalFile(input.absolutePath()+"/"+input.completeBaseName()+suffix);
}
QVariantMap Editor::namedOutput(const QString &raw,bool compress,int track,bool original) const {
    const QFileInfo suggested(suggestedOutput(compress,track,original).toLocalFile());
    QString name=raw.trimmed();if(name.isEmpty())name=suggested.fileName();
    static const QRegularExpression invalid(R"re([\x00-\x1F/\\:*?"<>|])re");
    static const QRegularExpression reserved(R"re(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))re",QRegularExpression::CaseInsensitiveOption);
    if(name=="."||name==".."||name.endsWith('.')||invalid.match(name).hasMatch()||reserved.match(name).hasMatch())return {{"error","Enter a filename, without a folder path or reserved characters."}};
    const QStringList extensions=track>=0?(original?QStringList{"mka"}:QStringList{"m4a","mka"}):QStringList{"mp4","mkv"};
    if(!extensions.contains(QFileInfo(name).suffix().toLower()))name+="."+suggested.suffix();
    const auto path=suggested.absoluteDir().filePath(name);
    if(!QFileInfo(path).canonicalFilePath().isEmpty()&&QFileInfo(path).canonicalFilePath()==QFileInfo(m_source.path).canonicalFilePath())return {{"error","Choose a different name to preserve the imported clip."}};
    return {{"url",QUrl::fromLocalFile(path)},{"name",name},{"error",QString()}};
}
void Editor::startExport(const QUrl &url,const QVariantMap &v) {
    auto p=planExport(m_source,m_edits,options(v),m_keys);if(!p.error.isEmpty()){emit notice(p.error);return;}
    if(m_job.running())return;
    m_exportRevision=p.options.extractTrack<0?m_revision:~quint64(0);
    m_exportState="running";m_exportPath=url.isLocalFile()?url.toLocalFile():url.toString();m_exportMessage.clear();emit changed();
    m_job.start(p,m_exportPath);
}
void Editor::dismissExport(){if(m_job.running())return;m_exportState.clear();m_exportMessage.clear();emit changed();}
void Editor::analyze(Source source,std::shared_ptr<std::atomic_bool> cancel,int generation) {
    m_tasks.append(QtConcurrent::run([this,source,cancel,generation] {
        try {
            const auto bytes=runTool(mediaTool("ffprobe"),{"-v","error","-select_streams","v:0","-show_packets","-show_entries","packet=pts_time,flags","-of","csv=p=0",source.path},cancel.get(),180000);
            QVector<Time> frames,keys;
            for(auto line:bytes.split('\n')){auto cells=line.split(',');bool ok=false;double time=cells.value(0).toDouble(&ok);if(ok){Time t=fromSeconds(time)-source.videoStart;if(t>=0){frames<<t;if(cells.value(1).contains('K'))keys<<t;}}}
            std::sort(frames.begin(),frames.end());std::sort(keys.begin(),keys.end());
            if(*cancel)return;
            QMetaObject::invokeMethod(this,[this,cancel,generation,frames,keys]{if(*cancel||generation!=m_generation)return;m_frames=frames;m_keys=keys;emit changed();});
        }catch(const std::exception &e){if(!*cancel){QString message=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,message]{emit logEvent("index_error",{{"message",message}});});}}
    }));
    m_tasks.append(QtConcurrent::run([this,source,cancel,generation] {
        for(int i=0;i<source.audio.size() && !*cancel;++i){
            QString alignment=QString("asetpts=PTS-(%1)/TB,aresample=8000:async=1:first_pts=0").arg(seconds(source.videoStart-source.formatStart),0,'f',9);
            QProcess process;process.start(mediaTool("ffmpeg"),{"-v","error","-i",source.path,"-map",QString("0:%1").arg(source.audio[i].streamIndex),"-vn","-af",alignment,"-ac","1","-ar","8000","-f","f32le","pipe:1"});
            if(!process.waitForStarted(5000))continue;
            QByteArray pending;QVariantList wave;quint64 count=0;float peak=0;const quint64 bucket=std::max<quint64>(80,seconds(source.duration)*8000/6000);
            const int bins=std::ceil(seconds(source.duration)*8000/bucket);wave.reserve(bins);for(int b=0;b<bins;++b)wave.append(0.0);
            QElapsedTimer updates;updates.start();
            while(process.state()!=QProcess::NotRunning){
                process.waitForReadyRead(50);pending+=process.readAllStandardOutput();process.readAllStandardError();
                int usable=pending.size()/4*4;for(int p=0;p<usable;p+=4){float sample;memcpy(&sample,pending.constData()+p,4);peak=std::max(peak,std::abs(sample));if(++count%bucket==0){int bin=count/bucket-1;if(bin<wave.size())wave[bin]=std::min(1.f,peak);peak=0;}}
                pending.remove(0,usable);
                if(*cancel){process.kill();process.waitForFinished(2000);return;}
                if(updates.elapsed()>500){updates.restart();QMetaObject::invokeMethod(this,[this,cancel,generation,i,wave]{if(*cancel||generation!=m_generation)return;m_waves[i]=wave;emit changed();});}
            }
            if(count%bucket&&int(count/bucket)<wave.size())wave[int(count/bucket)]=peak;
            QMetaObject::invokeMethod(this,[this,cancel,generation,i,wave]{if(*cancel||generation!=m_generation)return;m_waves[i]=wave;emit changed();});
        }
    }));
}
