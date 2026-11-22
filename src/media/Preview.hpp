#pragma once
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QQuickItem>
#include <QVariantList>
#include <QJsonObject>
#include <gst/gst.h>
#include <atomic>
#include "AudioMute.hpp"

class PlaybackWorker : public QObject {
    Q_OBJECT
public:
    explicit PlaybackWorker(GstElement *sink);
    ~PlaybackWorker() override;
public slots:
    void open(const QString &path, const QList<int> &ids);
    void shutdown();
    void setPlaying(bool value);
    void requestSeek(double seconds, bool exact);
    void setRouting(int solo, const QList<int> &included);
    void setRanges(const QVariantList &ranges);
    void setMutes(const TrackMutes &mutes);
signals:
    void position(double seconds);
    void frame(double seconds);
    void duration(double seconds);
    void playing(bool value);
    void diagnostics(const QString &text);
    void failure(const QString &text);
    void seekFinished(double milliseconds, bool exact);
    void audioTracks(int count);
    void performanceSample(const QJsonObject &data);
private:
    struct Branch { GstElement *volume; int index; };
    static void padAdded(GstElement *, GstPad *, gpointer);
    static GstPadProbeReturn observeFrame(GstPad *, GstPadProbeInfo *, gpointer);
    void poll();
    void dispatchSeek();
    void route();
    GstElement *m_sink = nullptr, *m_pipeline = nullptr, *m_videoQueue = nullptr, *m_mixer = nullptr;
    GstBus *m_bus = nullptr;
    QTimer *m_timer = nullptr;
    QList<Branch> m_audio;
    QList<int> m_included;
    QList<int> m_containerIds;
    std::shared_ptr<AudioMuteState> m_mutes=std::make_shared<AudioMuteState>();
    TrackMutes m_lastMutes;
    QVariantList m_ranges;
    int m_solo = -1;
    bool m_playing = false, m_seeking = false, m_pending = false, m_exact = true;
    bool m_started = false;
    double m_target = 0;
    qint64 m_seekStarted = 0;
    qint64 m_lastSample = 0;
    int m_segment = -1;
    GstSegment m_videoSegment;
    gulong m_probe = 0;
    bool m_dispatchedExact = true;
    std::atomic<quint64> m_buffers{0};
    std::atomic<int> m_generation{0};
    std::atomic<quint64> m_latestFrame{GST_CLOCK_TIME_NONE};
    std::atomic<int> m_audioCount{0};
};

class Preview : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double presented READ presented NOTIFY presentedChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit Preview(QObject *parent = nullptr);
    ~Preview() override;
    GstElement *sink() const { return m_sink; }
    double position() const { return m_position; }
    double presented() const { return m_presented; }
    double duration() const { return m_duration; }
    bool playing() const { return m_playing; }
    bool ready() const { return m_ready; }
    QString diagnostics() const { return m_diagnostics; }
    QString error() const { return m_error; }
    Q_INVOKABLE void attach(QQuickItem *item);
    Q_INVOKABLE void open(const QString &path, const QVariantList &ids = {});
    Q_INVOKABLE void play(bool value);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds, bool exact = true);
    Q_INVOKABLE void route(int solo, const QVariantList &included);
    Q_INVOKABLE void ranges(const QVariantList &ranges);
    void mutes(const TrackMutes &regions);
    void stop();
signals:
    void positionChanged(); void presentedChanged(); void durationChanged();
    void playingChanged(); void readyChanged(); void diagnosticsChanged(); void errorChanged();
    void seekMeasured(double milliseconds, bool exact);
    void tracksChanged(int count);
    void performanceSample(const QJsonObject &data);
private:
    void startWorker();
    GstElement *m_sink = nullptr;
    PlaybackWorker *m_worker = nullptr;
    QThread m_thread;
    double m_position = 0, m_presented = 0, m_duration = 0;
    bool m_playing = false, m_ready = false;
    QString m_diagnostics, m_error, m_pendingPath;
    QVariantList m_pendingIds;
};
