#include "Export.hpp"
#include <QtConcurrent>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include <stdexcept>
#include <algorithm>

namespace clipcut {
static QString timeArg(Time value) { return QString::number(seconds(value), 'f', 9); }
static void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
static bool sameFile(const QString &a,const QString &b) {
#ifdef Q_OS_WIN
    auto open=[](const QString &path){return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);};
    auto first=open(a),second=open(b);BY_HANDLE_FILE_INFORMATION x{},y{};
    bool same=first!=INVALID_HANDLE_VALUE&&second!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(first,&x)&&GetFileInformationByHandle(second,&y)&&x.dwVolumeSerialNumber==y.dwVolumeSerialNumber&&x.nFileIndexHigh==y.nFileIndexHigh&&x.nFileIndexLow==y.nFileIndexLow;
    if(first!=INVALID_HANDLE_VALUE)CloseHandle(first);if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);return same;
#else
    struct stat x{},y{};return stat(QFile::encodeName(a).constData(),&x)==0&&stat(QFile::encodeName(b).constData(),&y)==0&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;
#endif
}
QString mediaTool(const QString &name) {
    QString local = QCoreApplication::applicationDirPath() + '/' + name;
#ifdef Q_OS_WIN
    local += ".exe";
#endif
    if (QFileInfo::exists(local)) return local;
    return QStandardPaths::findExecutable(name);
}
QByteArray runTool(const QString &program, const QStringList &args, std::atomic_bool *cancel, int timeoutMs) {
    if (program.isEmpty()) fail("FFmpeg/ffprobe is missing. Use the complete portable package or install the tools.");
    QProcess process; process.start(program, args);
    if (!process.waitForStarted(5000)) fail(process.errorString());
    QElapsedTimer timer; timer.start(); QByteArray output, errors;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50); output += process.readAllStandardOutput(); errors += process.readAllStandardError();
        if ((cancel && *cancel) || timer.elapsed() > timeoutMs) {
            process.kill(); process.waitForFinished(3000); fail(cancel && *cancel ? "Cancelled" : "Media analysis timed out");
        }
    }
    output += process.readAllStandardOutput(); errors += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) fail(QString::fromUtf8(errors).right(4000));
    return output;
}
Source probeSource(const QString &path) {
    auto json = QJsonDocument::fromJson(runTool(mediaTool("ffprobe"), {"-v", "error", "-show_format", "-show_streams", "-of", "json", path})).object();
    auto source = sourceFromProbe(path, json);
    // Matroska stream IDs use TrackNumber, not ffprobe's ordinal or pad arrival order.
    QFile file(path);
    if(file.open(QIODevice::ReadOnly)) {
        auto data=file.read(8*1024*1024);
        auto integer=[&](int &pos,int maximum,bool keepMarker)->quint64 {
            if(pos>=data.size())return 0;
            unsigned char first=data[pos];int length=1;unsigned char marker=0x80;while(length<=maximum&&!(first&marker)){marker>>=1;++length;}
            if(length>maximum||pos+length>data.size()){pos=data.size();return 0;}
            quint64 value=keepMarker?first:first&~marker;++pos;for(int i=1;i<length;++i)value=(value<<8)|static_cast<unsigned char>(data[pos++]);return value;
        };
        auto header=[&](int &pos,quint64 &id,int &end)->bool {
            if(pos>=data.size())return false;id=integer(pos,4,true);quint64 size=integer(pos,8,false);
            end=int(std::min<quint64>(data.size(),quint64(pos)+size));return id!=0;
        };
        int pos=0,end=0;quint64 id=0;
        if(header(pos,id,end)&&id==0x1a45dfa3){
            pos=end;
            if(header(pos,id,end)&&id==0x18538067){
                while(pos<data.size()){
                    int next=0;if(!header(pos,id,next))break;
                    if(id==0x1654ae6b){
                        int ordinal=0;
                        while(pos<next){int entryEnd=0;if(!header(pos,id,entryEnd))break;
                            if(id==0xae){quint64 number=0,type=0;while(pos<entryEnd){int fieldEnd=0;if(!header(pos,id,fieldEnd))break;quint64 value=0;for(int j=pos;j<fieldEnd&&j<pos+8;++j)value=(value<<8)|static_cast<unsigned char>(data[j]);if(id==0xd7)number=value;else if(id==0x83)type=value;pos=fieldEnd;}
                                if(type==2&&ordinal<source.audio.size())source.audio[ordinal++].containerId=int(number);
                            }pos=entryEnd;
                        }break;
                    }
                    if(next<=pos)break;pos=next;
                }
            }
        }
    }
    if (source.videoIndex < 0 || source.duration <= 0 || source.width <= 0) fail("The file does not contain a readable video stream with a known duration.");
    return source;
}
ExportPlan planExport(const Source &s, const Edits &e, const ExportOptions &o, const QVector<Time> &keys) {
    ExportPlan p; p.source = s; p.edits = e; p.options = o;
    if (o.extractTrack >= 0) {
        if (o.extractTrack >= s.audio.size()) { p.error = "That audio track is unavailable."; return p; }
        p.edits.included = {o.extractTrack}; p.edits.collapse = false;
        if (o.original) { p.edits.trim = {0, s.duration}; p.edits.deletes.clear();p.edits.mutes.clear(); }
    }
    p.ranges = kept(p.edits);
    if (p.ranges.isEmpty()) { p.error = "No video remains. Adjust the trim or delete ranges."; return p; }
    bool temporal = temporalEdits(s, p.edits);
    bool cropped = e.crop != QRect(0, 0, s.width, s.height);
    p.copyVideo = o.extractTrack < 0 && !cropped && !o.compress && !o.forceEncode && !temporal;
    if (o.fastCopy && temporal && !cropped && !o.compress && !o.forceEncode && o.extractTrack < 0) {
        if (keys.isEmpty()) { p.error = "Keyframe analysis is still running. Try again when it finishes."; return p; }
        if (s.videoCodec != "h264") { p.error = "Fast copy cuts are validated for H.264 in v0.1. Choose Frame accurate for this codec."; return p; }
        auto snap = [&](Time value) {
            if (value <= 0) return Time(0);
            if (value >= s.duration) return s.duration;
            auto it = std::lower_bound(keys.begin(), keys.end(), value);
            if (it == keys.end()) return s.duration;
            if (it == keys.begin()) return *it;
            return value - *(it - 1) < *it - value ? *(it - 1) : *it;
        };
        for (auto &r : p.ranges) { r.start = snap(r.start); r.end = snap(r.end); }
        p.ranges = normalize(p.ranges); p.copyVideo = true;
        if (p.ranges.isEmpty()) { p.error = "Snapping leaves no retained content. Use Frame accurate."; return p; }
    }
    for (auto r : p.ranges) p.duration += r.end - r.start;
    p.audioOutputs = p.edits.included.isEmpty() ? 0 : p.edits.collapse ? 1 : p.edits.included.size();
    for(auto track:p.edits.included) {
        bool hasMute=false;for(auto r:p.ranges)if(!muted(p.edits,track,r).isEmpty()){hasMute=true;break;}
        if(!temporal&&!o.compress&&!(p.edits.collapse&&p.edits.included.size()>1)&&!hasMute)p.copiedAudio.append(track);
    }
    p.copyAudio=p.copiedAudio.size()==p.edits.included.size();
    if (o.compress) {
        p.videoKbps = int(o.targetMB * 8000 * 0.97 / seconds(p.duration)) - p.audioOutputs * 128;
        if (p.videoKbps < 150) { p.error = "Target size is too small for the kept duration and audio budget. Increase MB or shorten the clip."; return p; }
    }
    p.summary = o.extractTrack >= 0 ? (p.copyAudio ? "Audio: original codec, copied" : "Audio: edited, encode AAC") :
        QString("Video: %1 · Audio: %2").arg(p.copyVideo ? "copy" : o.compress ? "two-pass H.264" : "encode H.264", p.audioOutputs == 0 ? "none" : p.copyAudio ? "copy" : !p.copiedAudio.isEmpty()?"copy + encode AAC":p.edits.collapse && p.edits.included.size() > 1 ? "mix to AAC" : "encode AAC");
    return p;
}
ExportJob::ExportJob(QObject *parent) : QObject(parent) {}
ExportJob::~ExportJob() { cancel();m_thread.quit();m_thread.wait(); }
void ExportJob::cancel() { m_cancel = true; }
void ExportJob::start(const ExportPlan &plan, const QString &output) {
    if (m_running) return;
    m_cancel = false; m_running = true; m_progress = 0; m_status = "Preparing export"; emit changed();
    if(m_thread.isRunning()){m_thread.quit();m_thread.wait();}
    m_task=new QObject;m_task->moveToThread(&m_thread);
    connect(&m_thread,&QThread::finished,m_task,&QObject::deleteLater);
    m_thread.start();
    QMetaObject::invokeMethod(m_task,[this, plan, output] {
        QString message; bool success = false;
        try { message = execute(plan, output); success = true; }
        catch (const std::exception &error) { message = QString::fromUtf8(error.what()); }
        QMetaObject::invokeMethod(this, [this, success, output, message] {
            m_running = false; m_status = message; if (success) m_progress = 1; emit changed(); emit finished(success, output, message);
        });
        m_thread.quit();
    },Qt::QueuedConnection);
}
void ExportJob::ffmpeg(const QStringList &args, double base, double weight, Time length, const QString &stage) {
    if (m_cancel) fail("Cancelled");
    QMetaObject::invokeMethod(this, [this, stage] { m_status = stage; emit changed(); });
    // Leave CPU and thread capacity for the preview and progressive analysis.
    QStringList options{"-hide_banner", "-loglevel", qEnvironmentVariableIsSet("CLIPCUT_FFMPEG_DEBUG")?"debug":"error", "-y", "-nostdin", "-progress", "pipe:1", "-nostats", "-filter_threads", "2", "-filter_complex_threads", "2", "-threads", "4"}; options += args;
    if(!args.isEmpty()){options.insert(options.size()-1,"-threads");options.insert(options.size()-1,"4");}
    QProcess process; process.start(mediaTool("ffmpeg"), options);
    if (!process.waitForStarted(5000)) fail(process.errorString());
    QByteArray pending, errors; QElapsedTimer clock; clock.start();
    emit logEvent("export_stage", {{"stage", stage}, {"arguments", QJsonArray::fromStringList(options)}});
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50); pending += process.readAllStandardOutput(); errors += process.readAllStandardError(); errors = errors.right(64000);
        while (pending.contains('\n')) {
            int end = pending.indexOf('\n'); auto line = pending.left(end); pending.remove(0, end + 1);
            if (line.startsWith("out_time_us=")) {
                double value = std::clamp(base + weight * line.mid(12).toDouble() * 1000 / std::max(Time(1), length), base, base + weight);
                QMetaObject::invokeMethod(this, [this, value] { m_progress = value; emit changed(); });
            }
        }
        if (m_cancel) { process.kill(); process.waitForFinished(3000); fail("Cancelled"); }
    }
    errors += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) fail(stage + ": " + QString::fromUtf8(errors).right(4000));
    emit logEvent("export_stage_complete", {{"stage", stage}, {"elapsed_ms", clock.elapsed()}});
}
QString ExportJob::execute(const ExportPlan &p, const QString &output,int attempt) {
    if (!p.error.isEmpty()) fail(p.error);
    if (output.isEmpty()) fail("Choose an output file.");
    if (sameFile(p.source.path,output) || QFileInfo(output).canonicalFilePath() == QFileInfo(p.source.path).canonicalFilePath()) fail("Output must differ from the source.");
    if (QFileInfo::exists(output) && !p.options.overwrite) fail("Output exists. Enable overwrite in the export sheet to replace it.");
    if(p.copyVideo && temporalEdits(p.source,p.edits)) {
        for(auto r:p.ranges){
            QStringList args{"-v","error"};if(r.start>0)args += {"-ss",timeArg(r.start+p.source.videoStart-p.source.formatStart)};
            args += {"-i",p.source.path,"-map",QString("0:%1").arg(p.source.videoIndex),"-frames:v","1","-c:v","copy","-bsf:v","h264_mp4toannexb","-f","h264","pipe:1"};
            auto packet=runTool(mediaTool("ffmpeg"),args,&m_cancel);bool idr=false;
            for(int i=0;i+4<packet.size();++i){
                int offset=0;if(packet[i]==0&&packet[i+1]==0&&packet[i+2]==1)offset=3;else if(packet[i]==0&&packet[i+1]==0&&packet[i+2]==0&&packet[i+3]==1)offset=4;
                if(offset){int type=static_cast<unsigned char>(packet[i+offset])&31;if(type>=1&&type<=5){idr=type==5;break;}}
            }
            if(!idr)fail("A snapped boundary is not an independently decodable H.264 IDR. Choose Frame accurate for this recording.");
        }
    }
    QTemporaryDir temp(QFileInfo(output).absolutePath() + "/.clipcut-XXXXXX");
    if (!temp.isValid()) fail("Could not create temporary files beside the output.");
    QString staged = temp.filePath("result." + QFileInfo(output).suffix());
    if (p.options.original && p.options.extractTrack >= 0) {
        ffmpeg({"-i", p.source.path, "-map", QString("0:%1").arg(p.source.audio[p.options.extractTrack].streamIndex), "-c:a", "copy", "-vn", staged}, 0, .95, p.duration, "Extract original audio");
    } else {
        QString videoPath, audioPath;
        QList<int> encodedAudio;for(auto track:p.edits.included)if(!p.copiedAudio.contains(track))encodedAudio.append(track);
        auto concat = [&](const QStringList &paths, const QString &name, bool video) {
            if (paths.size() == 1) return paths.first();
            QString listPath = temp.filePath(name + ".txt"); QFile list(listPath); if (!list.open(QIODevice::WriteOnly)) fail(list.errorString());
            for (auto path : paths) { QString escaped = path; escaped.replace("'", "'\\''"); list.write(("file '" + escaped + "'\n").toUtf8()); }
            list.close(); QString dest = temp.filePath(name + ".mkv");
            ffmpeg({"-f", "concat", "-safe", "0", "-i", listPath, "-map", "0", "-c", "copy", dest}, .72, .02, p.duration, video ? "Join retained video" : "Join retained audio");
            return dest;
        };
        if (p.options.extractTrack < 0 && p.copyVideo && !temporalEdits(p.source,p.edits))videoPath=p.source.path;
        else if (p.options.extractTrack < 0) {
            QStringList segments;
            for (int i = 0; i < p.ranges.size(); ++i) {
                auto r = p.ranges[i]; QString dest = temp.filePath(QString("video-%1.mkv").arg(i));
                QStringList args;
                if(!p.copyVideo && !p.options.compress && p.options.profile=="vaapi") args += {"-vaapi_device","/dev/dri/renderD128"};
                if (r.start > 0 || p.source.videoStart!=p.source.formatStart) args += {"-ss", timeArg(r.start+p.source.videoStart-p.source.formatStart)};
                args += {"-i", p.source.path, "-t", timeArg(r.end - r.start), "-map", QString("0:%1").arg(p.source.videoIndex), "-an"};
                if (p.copyVideo) args += {"-c:v", "copy", "-avoid_negative_ts", "make_zero"};
                else {
                    QStringList filters;
                    if (p.edits.crop != QRect(0, 0, p.source.width, p.source.height)) {
                        const auto c = p.edits.crop; filters << QString("crop=%1:%2:%3:%4").arg(c.width()).arg(c.height()).arg(c.x()).arg(c.y());
                    }
                    if (p.options.compress && p.options.scale != 1) filters << QString("scale=trunc(iw*%1/2)*2:trunc(ih*%1/2)*2:flags=lanczos").arg(p.options.scale);
                    if (p.options.compress && p.options.reduceFps && p.source.fps() > 30.001) filters << "fps=30";
                    filters << ((!p.options.compress && p.options.profile=="vaapi")?"format=nv12,hwupload":"format=yuv420p");
                    args += {"-vf", filters.join(',')};
                    if (!p.options.compress && p.options.profile == "nvenc") args += {"-c:v", "h264_nvenc", "-preset", "p4", "-cq", "20", "-b:v", "0"};
                    else if (!p.options.compress && p.options.profile == "qsv") args += {"-c:v", "h264_qsv", "-global_quality", "20"};
                    else if (!p.options.compress && p.options.profile == "vaapi") args += {"-c:v", "h264_vaapi", "-qp", "20"};
                    else args += {"-c:v", "libx264", "-preset", p.options.compress ? "slow" : p.options.profile == "cpu-balanced" ? "medium" : "fast"};
                    if (p.options.compress) {
                        args += {"-b:v", QString::number(p.videoKbps) + "k"};
                        QString passlog = temp.filePath(QString("pass-%1").arg(i));
                        auto pass1 = args; pass1 += {"-pass", "1", "-passlogfile", passlog, "-f", "null",
#ifdef Q_OS_WIN
                            "NUL"
#else
                            "/dev/null"
#endif
                        };
                        ffmpeg(pass1, .65 * i / p.ranges.size(), .3 / p.ranges.size(), r.end - r.start, "Compression pass 1");
                        args += {"-pass", "2", "-passlogfile", passlog};
                    } else if (p.options.profile.startsWith("cpu")) args += {"-crf", "20"};
                }
                args << dest;
                ffmpeg(args, .65 * i / p.ranges.size() + (p.options.compress ? .3 / p.ranges.size() : 0), (p.options.compress ? .35 : .65) / p.ranges.size(), r.end - r.start, p.copyVideo ? "Copy retained video" : "Encode retained video");
                segments << dest;
            }
            videoPath = concat(segments, "video", true);
        }
        if (p.audioOutputs > 0) {
            if (p.copyAudio) audioPath = p.source.path;
            else {
                QStringList segments;
                for (int i = 0; i < p.ranges.size(); ++i) {
                    auto r = p.ranges[i]; QString dest = temp.filePath(QString("audio-%1.mka").arg(i)); QStringList args;
                    if (r.start > 0 || p.source.videoStart!=p.source.formatStart) args += {"-ss", timeArg(r.start+p.source.videoStart-p.source.formatStart)};
                    args += {"-i", p.source.path, "-t", timeArg(r.end - r.start), "-vn"};
                    QString graph; QStringList tracks;
                    for (int j = 0; j < encodedAudio.size(); ++j) {
                        const auto track = p.source.audio[encodedAudio[j]];
                        QString label = "a" + QString::number(j); tracks << '[' + label + ']';
                        QString muteFilter;QStringList conditions;
                        for(auto interval:muted(p.edits,encodedAudio[j],r))conditions<<QString("gte(t,%1)*lt(t,%2)").arg(timeArg(interval.start-r.start),timeArg(interval.end-r.start));
                        if(!conditions.isEmpty())muteFilter=",aeval=exprs='if("+conditions.join('+')+",0,val(ch))':c=same";
                        graph += QString("[0:%1]aresample=48000:async=1:first_pts=0,aformat=sample_fmts=flt:channel_layouts=stereo,apad=whole_dur=%2,atrim=duration=%2%3[%4];").arg(track.streamIndex).arg(timeArg(r.end - r.start),muteFilter,label);
                    }
                    if (p.edits.collapse && tracks.size() > 1) {
                        graph += tracks.join("") + QString("amix=inputs=%1:normalize=0:duration=longest,volume=%2[mix]").arg(tracks.size()).arg(1.0 / tracks.size(), 0, 'f', 9);
                        args += {"-filter_complex", graph, "-map", "[mix]"};
                    } else {
                        graph.chop(1); args += {"-filter_complex", graph}; for (auto label : tracks) args += {"-map", label};
                    }
                    args += {"-c:a", "pcm_f32le", dest};
                    ffmpeg(args, .74 + .1 * i / p.ranges.size(), .1 / p.ranges.size(), r.end - r.start, "Prepare edited audio"); segments << dest;
                }
                audioPath = concat(segments, "audio", false);
            }
        }
        QStringList args;
        if (!videoPath.isEmpty()) args += {"-i", videoPath};
        int audioInput = videoPath.isEmpty() ? 0 : 1;
        if (!audioPath.isEmpty()) args += {"-i", audioPath};
        int copiedInput=audioInput;
        if(!p.copyAudio&&!p.copiedAudio.isEmpty()){copiedInput=audioInput+1;args += {"-i",p.source.path};}
        if (!videoPath.isEmpty()) args += {"-map",videoPath==p.source.path?QString("0:%1").arg(p.source.videoIndex):"0:v:0", "-c:v", "copy"};
        if (!audioPath.isEmpty()) {
            if(p.edits.collapse&&p.edits.included.size()>1)args += {"-map",QString("%1:a:0").arg(audioInput),"-c:a","aac","-b:a","128k"};
            else for(int j=0;j<p.edits.included.size();++j) {
                const auto index=p.edits.included[j];const bool copy=p.copiedAudio.contains(index);
                args += {"-map",copy?QString("%1:%2").arg(copiedInput).arg(p.source.audio[index].streamIndex):QString("%1:a:%2").arg(audioInput).arg(encodedAudio.indexOf(index)),QString("-c:a:%1").arg(j),copy?"copy":"aac"};
                if(!copy)args += {QString("-b:a:%1").arg(j),"128k"};
            }
        }
        if (QFileInfo(output).suffix().toLower() == "mp4" || QFileInfo(output).suffix().toLower() == "m4a") args += {"-movflags", "+faststart"};
        args << staged; ffmpeg(args, .85, .1, p.duration, "Finalize output");
    }
    auto json = QJsonDocument::fromJson(runTool(mediaTool("ffprobe"), {"-v", "error", "-show_streams", "-show_format", "-of", "json", staged}, &m_cancel)).object();
    auto streams = json["streams"].toArray(); int audio = 0, video = 0;
    for (auto v : streams) { auto type = v.toObject()["codec_type"].toString(); audio += type == "audio"; video += type == "video"; }
    if (audio != p.audioOutputs || video != (p.options.extractTrack >= 0 ? 0 : 1)) fail("Output verification found unexpected stream counts.");
    const double duration = json["format"].toObject()["duration"].toString().toDouble();
    if (std::abs(duration - seconds(p.duration)) > .15) fail("Output duration differs from the retained timeline; output was not published.");
    if(p.options.extractTrack<0) {
        const auto videoInfo=streams.first().toObject();
        int width=p.copyVideo?p.source.width:p.edits.crop.width(),height=p.copyVideo?p.source.height:p.edits.crop.height();
        if(p.options.compress){width=int(width*p.options.scale)/2*2;height=int(height*p.options.scale)/2*2;}
        if(videoInfo["width"].toInt()!=width||videoInfo["height"].toInt()!=height)fail("Output dimensions differ from the export plan.");
        Time join=0;
        for(int i=0;i<p.ranges.size();++i){
            QStringList check{"-v","error","-xerror","-ss",timeArg(std::max(Time(0),join-Second/2)),"-i",staged,"-t","1","-map","0:v:0","-an","-f","null","-"};
            runTool(mediaTool("ffmpeg"),check,&m_cancel,60000);join+=p.ranges[i].end-p.ranges[i].start;
        }
    }
    if (m_cancel) fail("Cancelled");
    if (p.options.compress && QFileInfo(staged).size() > p.options.targetMB * 1000000) {
        if(attempt>=2)fail("Output exceeds the target after two corrections. Increase the target or reduce dimensions; the destination was preserved.");
        auto corrected=p;corrected.videoKbps=int(p.videoKbps*.9*p.options.targetMB*1000000/QFileInfo(staged).size());
        if(corrected.videoKbps<150)fail("Target cannot fit the encoded audio and video at the minimum bitrate. Increase MB; the destination was preserved.");
        emit logEvent("compression_correction",{{"attempt",attempt+1},{"actual_bytes",QFileInfo(staged).size()},{"video_kbps",corrected.videoKbps}});
        temp.remove();return execute(corrected,output,attempt+1);
    }
    const auto backup=temp.filePath("previous-output");
    const bool existing=QFileInfo::exists(output);
    if(existing && !QFile::rename(output,backup))fail("Could not preserve the previous destination before replacing it.");
    if (!QFile::rename(staged, output)) {if(existing)QFile::rename(backup,output);fail("Could not move the verified output into place.");}
    return "Saved " + output;
}
}
