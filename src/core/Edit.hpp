#pragma once
#include <QList>
#include <QRect>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QHash>

namespace clipcut {
using Time = qint64;
constexpr Time Second = 1000000000;
inline Time fromSeconds(double s) { return qRound64(s * Second); }
inline double seconds(Time t) { return double(t) / Second; }
struct Interval {
    Time start = 0, end = 0;
    bool operator==(const Interval &) const = default;
};
struct AudioTrack {
    int streamIndex = -1;
    int containerId = -1;
    QString title, codec, layout;
    int sampleRate = 0, channels = 0;
    Time start = 0;
};
struct Source {
    QString path, videoCodec;
    int width = 0, height = 0, videoIndex = -1, fpsNum = 0, fpsDen = 1;
    int sarNum = 1, sarDen = 1, rotation = 0;
    Time duration = 0, videoStart = 0, formatStart = 0;
    QList<AudioTrack> audio;
    double fps() const { return fpsDen ? double(fpsNum) / fpsDen : 0; }
};
struct Edits {
    Interval trim;
    QList<Interval> deletes;
    QHash<int,QList<Interval>> mutes;
    QRect crop;
    QList<int> included;
    bool collapse = true;
    bool operator==(const Edits &) const = default;
};
QList<Interval> normalize(QList<Interval> intervals);
// Merge completed regions and remap selection; retain unfinished start markers.
int mergeRegions(QList<Interval> &intervals, int selected = -1);
QList<Interval> muted(const Edits &edits, int track, Interval range);
QList<Interval> kept(const Edits &edits);
Time keptDuration(const Edits &edits);
Time sourceToOutput(const Edits &edits, Time sourceTime);
Time outputToSource(const Edits &edits, Time outputTime);
bool temporalEdits(const Source &source, const Edits &edits);
Edits initialEdits(const Source &source);
Source sourceFromProbe(const QString &path, const QJsonObject &json);
QRect constrainedCrop(QRect crop, QSize size, bool even = true);

class History {
public:
    void reset(const Edits &initial);
    bool commit(const Edits &value);
    Edits undo(); Edits redo();
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor + 1 < m_values.size(); }
    const Edits &current() const { return m_values[m_cursor]; }
private:
    QList<Edits> m_values;
    int m_cursor = 0;
};
}
