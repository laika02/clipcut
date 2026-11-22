#pragma once
#include "core/Edit.hpp"
#include <QObject>
#include <QFuture>
#include <QThread>
#include <atomic>

namespace clipcut {
struct ExportOptions {
    QString profile = "cpu-fast";
    bool fastCopy = false, forceEncode = false, compress = false, reduceFps = true;
    double scale = 1, targetMB = 20;
    int extractTrack = -1;
    bool original = false, overwrite = false;
};
struct ExportPlan {
    Source source;
    Edits edits;
    ExportOptions options;
    QList<Interval> ranges;
    bool copyVideo = false, copyAudio = false;
    QList<int> copiedAudio;
    QString error, summary;
    Time duration = 0;
    int videoKbps = 0, audioOutputs = 0;
};
ExportPlan planExport(const Source &source, const Edits &edits, const ExportOptions &options, const QVector<Time> &keys = {});
QString mediaTool(const QString &name);
QByteArray runTool(const QString &program, const QStringList &arguments, std::atomic_bool *cancel = nullptr, int timeoutMs = 30000);
Source probeSource(const QString &path);

class ExportJob : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit ExportJob(QObject *parent = nullptr);
    ~ExportJob() override;
    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    void start(const ExportPlan &plan, const QString &output);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void finished(bool success, const QString &path, const QString &message);
    void logEvent(const QString &type, const QJsonObject &data);
private:
    QString execute(const ExportPlan &plan, const QString &output, int attempt = 0);
    void ffmpeg(const QStringList &args, double base, double weight, Time length, const QString &stage);
    QThread m_thread;
    QObject *m_task = nullptr;
    std::atomic_bool m_cancel{false};
    bool m_running = false;
    double m_progress = 0;
    QString m_status;
};
}
