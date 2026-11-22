#pragma once
#include "core/Edit.hpp"
#include "export/Export.hpp"
#include "media/Preview.hpp"
#include <QFuture>
#include <QSettings>
#include <QPointer>
#include <memory>

class Editor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY preferencesChanged)
    Q_PROPERTY(bool fastCopy READ fastCopy WRITE setFastCopy NOTIFY preferencesChanged)
    Q_PROPERTY(bool forceEncode READ forceEncode WRITE setForceEncode NOTIFY preferencesChanged)
    Q_PROPERTY(bool muteDragging READ muteDragging NOTIFY muteDragChanged)
    Q_PROPERTY(QObject* job READ job CONSTANT)
public:
    explicit Editor(Preview *preview);
    ~Editor() override;
    QVariantMap state() const;
    QVariantList commands() const;
    QVariantList profiles() const { return m_profiles; }
    QString profile() const { return m_settings.value("profile","cpu-fast").toString(); }
    bool fastCopy() const { return m_settings.value("fastCopy",false).toBool(); }
    bool forceEncode() const { return m_settings.value("forceEncode",false).toBool(); }
    bool muteDragging() const { return m_muteDragTrack>=0; }
    void setProfile(const QString &value);
    void setFastCopy(bool value);
    void setForceEncode(bool value);
    QObject *job() { return &m_job; }
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void dispatch(const QString &id);
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void trim(double start, double end);
    Q_INVOKABLE void crop(double x, double y, double width, double height);
    Q_INVOKABLE void setDelete(int index, double start, double end);
    Q_INVOKABLE int addDelete(double start, double end);
    Q_INVOKABLE void moveDelete(int index, double start);
    Q_INVOKABLE void selectDelete(int index);
    Q_INVOKABLE int addMute(int track, double start, double end);
    Q_INVOKABLE void setMute(int track, int index, double start, double end);
    Q_INVOKABLE void moveMute(int track, int index, double start);
    Q_INVOKABLE void selectMute(int track, int index);
    Q_INVOKABLE void removeMute(int track, int index);
    Q_INVOKABLE void activateTrack(int track);
    Q_INVOKABLE void beginMuteDrag(int track, int index, int part, QQuickItem *timeline, double x);
    Q_INVOKABLE void noteMuteGrabLoss();
    Q_INVOKABLE void include(int index);
    Q_INVOKABLE void solo(int index, bool play = false);
    Q_INVOKABLE void scrub(double time, int track = -1, bool settle = false);
    Q_INVOKABLE void collapse(bool value);
    Q_INVOKABLE QString bind(const QString &id, const QString &sequence);
    Q_INVOKABLE void resetBindings();
    Q_INVOKABLE QVariantMap exportSummary(const QVariantMap &options) const;
    Q_INVOKABLE QUrl suggestedOutput(bool compress = false, int track = -1, bool original = false) const;
    Q_INVOKABLE QVariantMap namedOutput(const QString &name, bool compress = false, int track = -1, bool original = false) const;
    Q_INVOKABLE void startExport(const QUrl &output, const QVariantMap &options);
    Q_INVOKABLE void dismissExport();
signals:
    void changed(); void bindingsChanged(); void profilesChanged(); void preferencesChanged();
    void request(const QString &action);
    void notice(const QString &message);
    void logEvent(const QString &type, const QJsonObject &data);
    void muteDragChanged();
    void muteDragFinished(int track, int part);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void update(bool commit = true);
    void moveMuteDrag(double sceneX);
    void finishMuteDrag();
    void analyze(clipcut::Source source, std::shared_ptr<std::atomic_bool> cancel, int generation);
    clipcut::ExportOptions options(const QVariantMap &values) const;
    Preview *m_preview;
    clipcut::Source m_source;
    clipcut::Edits m_edits;
    clipcut::History m_history;
    clipcut::ExportJob m_job;
    QSettings m_settings;
    QVariantList m_profiles;
    QHash<int,QVariantList> m_waves;
    QVector<clipcut::Time> m_keys, m_frames;
    QList<QFuture<void>> m_tasks;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_loading = false, m_gesture = false, m_dirty = false, m_scrubbing = false, m_resume = false;
    int m_generation = 0, m_solo = -1, m_selectedDelete = -1;
    int m_activeTrack = -1, m_selectedMuteTrack = -1, m_selectedMute = -1;
    int m_blockedGestureScrolls = 0;
    int m_muteDragTrack=-1, m_muteDragIndex=-1, m_muteDragPart=-1;
    QPointer<QQuickWindow> m_muteDragWindow;
    clipcut::Interval m_muteDragRange;
    double m_muteDragPressX=0, m_muteDragSecondsPerPixel=0;
    quint64 m_revision = 0, m_exportRevision = 0;
    QString m_exportState,m_exportPath,m_exportMessage;
};
