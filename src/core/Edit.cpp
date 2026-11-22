#include "Edit.hpp"
#include <QJsonArray>
#include <algorithm>

namespace clipcut {
QList<Interval> normalize(QList<Interval> intervals) {
    std::sort(intervals.begin(), intervals.end(), [](auto a, auto b) { return a.start < b.start; });
    QList<Interval> result;
    for (auto i : intervals) {
        if (i.end <= i.start) continue;
        if (!result.isEmpty() && i.start <= result.last().end) result.last().end = std::max(result.last().end, i.end);
        else result.append(i);
    }
    return result;
}
QList<Interval> kept(const Edits &edits) {
    QList<Interval> result;
    Time cursor = edits.trim.start;
    for (auto i : normalize(edits.deletes)) {
        i.start = std::max(i.start, edits.trim.start); i.end = std::min(i.end, edits.trim.end);
        if (i.end <= i.start) continue;
        if (i.start > cursor) result.append({cursor, i.start});
        cursor = std::max(cursor, i.end);
    }
    if (cursor < edits.trim.end) result.append({cursor, edits.trim.end});
    return result;
}
int mergeRegions(QList<Interval> &intervals,int selected) {
    const bool selecting=selected>=0&&selected<intervals.size();
    const auto selection=selecting?intervals[selected]:Interval{};
    auto result=normalize(intervals);
    // '[' creates a zero-length start marker until ']' supplies its end.
    for(auto r:intervals)if(r.start==r.end)result.append(r);
    std::stable_sort(result.begin(),result.end(),[](auto a,auto b){return a.start<b.start;});
    int next=-1;
    if(selecting)for(int i=0;i<result.size();++i) {
        const auto r=result[i];
        if(selection.start==selection.end?r==selection:r.start<=selection.start&&r.end>=selection.end){next=i;break;}
    }
    intervals=std::move(result);return next;
}
QList<Interval> muted(const Edits &edits,int track,Interval range) {
    QList<Interval> intervals;
    for(auto r:edits.mutes.value(track))intervals.append({std::max(r.start,range.start),std::min(r.end,range.end)});
    return normalize(intervals);
}
Time keptDuration(const Edits &edits) { Time t = 0; for (auto r : kept(edits)) t += r.end - r.start; return t; }
Time sourceToOutput(const Edits &edits, Time t) {
    Time out = 0;
    for (auto r : kept(edits)) { if (t <= r.end) return out + std::clamp(t - r.start, Time(0), r.end - r.start); out += r.end - r.start; }
    return out;
}
Time outputToSource(const Edits &edits, Time t) {
    auto ranges = kept(edits);
    for (auto r : ranges) { if (t < r.end - r.start) return r.start + std::max(Time(0), t); t -= r.end - r.start; }
    return ranges.isEmpty() ? edits.trim.start : ranges.last().end;
}
bool temporalEdits(const Source &source, const Edits &edits) {
    return kept(edits) != QList<Interval>{{0, source.duration}};
}
Edits initialEdits(const Source &source) {
    Edits edits; edits.trim = {0, source.duration}; edits.crop = QRect(0, 0, source.width, source.height);
    for (int i = 0; i < source.audio.size(); ++i) edits.included.append(i);
    return edits;
}
static Time jsonTime(const QJsonValue &value) { bool ok = false; double d = value.toString().toDouble(&ok); return ok ? fromSeconds(d) : 0; }
Source sourceFromProbe(const QString &path, const QJsonObject &json) {
    Source result; result.path = path;
    result.duration = jsonTime(json["format"].toObject()["duration"]);
    result.formatStart=jsonTime(json["format"].toObject()["start_time"]);
    bool hasVideoDuration=false;
    for (auto entry : json["streams"].toArray()) {
        auto stream = entry.toObject(); const QString type = stream["codec_type"].toString();
        if (type == "video" && result.videoIndex < 0 && stream["disposition"].toObject()["attached_pic"].toInt() == 0) {
            result.videoIndex = stream["index"].toInt(); result.videoCodec = stream["codec_name"].toString();
            result.width = stream["width"].toInt(); result.height = stream["height"].toInt();
            auto rate = stream["avg_frame_rate"].toString().split('/');
            result.fpsNum = rate.value(0).toInt(); result.fpsDen = std::max(1, rate.value(1).toInt());
            auto sar = stream["sample_aspect_ratio"].toString().split(':');
            result.sarNum = std::max(1, sar.value(0).toInt()); result.sarDen = std::max(1, sar.value(1).toInt());
            result.videoStart = jsonTime(stream["start_time"]);
            for (auto side : stream["side_data_list"].toArray()) if (side.toObject().contains("rotation")) result.rotation = side.toObject()["rotation"].toInt();
            Time duration = jsonTime(stream["duration"]); if (duration > 0) {result.duration = duration;hasVideoDuration=true;}
        } else if (type == "audio") {
            AudioTrack track; track.streamIndex = stream["index"].toInt(); track.codec = stream["codec_name"].toString();
            bool idOk=false;track.containerId=stream["id"].toString().toInt(&idOk,0);if(!idOk)track.containerId=-1;
            track.title = stream["tags"].toObject()["title"].toString(); if (track.title.isEmpty()) track.title = "Track " + QString::number(result.audio.size() + 1);
            track.channels = stream["channels"].toInt(); track.layout = stream["channel_layout"].toString();
            track.sampleRate = stream["sample_rate"].toString().toInt(); track.start = jsonTime(stream["start_time"]);
            result.audio.append(track);
        }
    }
    if(!hasVideoDuration) {
        const auto format=json["format"].toObject()["format_name"].toString();
        result.duration-=std::max(Time(0),format.contains("matroska")?result.videoStart:result.videoStart-result.formatStart);
    }
    return result;
}
QRect constrainedCrop(QRect crop, QSize size, bool even) {
    const int alignment = even ? 2 : 1;
    int x = std::clamp(crop.x(), 0, std::max(0, size.width() - alignment));
    int y = std::clamp(crop.y(), 0, std::max(0, size.height() - alignment));
    int w = std::clamp(crop.width(), alignment, size.width() - x);
    int h = std::clamp(crop.height(), alignment, size.height() - y);
    if (even) { x -= x % 2; y -= y % 2; w -= w % 2; h -= h % 2; }
    return QRect(x, y, w, h);
}
void History::reset(const Edits &initial) { m_values = {initial}; m_cursor = 0; }
bool History::commit(const Edits &value) {
    if (value == current()) return false;
    while (m_values.size() > m_cursor + 1) m_values.removeLast();
    m_values.append(value); ++m_cursor;
    if (m_values.size() > 201) { m_values.removeFirst(); --m_cursor; }
    return true;
}
Edits History::undo() { if (canUndo()) --m_cursor; return current(); }
Edits History::redo() { if (canRedo()) ++m_cursor; return current(); }
}
