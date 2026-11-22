#pragma once
#include <QObject>
#include <QFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QQuickWindow>
#include "media/Preview.hpp"

class PerformanceLog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
public:
    PerformanceLog(const QString &path, Preview *preview, QQuickWindow *window);
    ~PerformanceLog() override;
    void event(const QString &type, QJsonObject data = {});
    QString path() const { return m_file.isOpen() ? m_file.fileName() : QString(); }
    Q_INVOKABLE void setPath(const QString &path);
    void observeWindow(QQuickWindow *window);
signals:
    void pathChanged();
private:
    QFile m_file;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QVector<double> m_frames;
    qint64 m_previousFrame = 0;
    bool m_firstFrame = false;
    qint64 m_previousCpu = 0, m_previousSampleMs = 0;
    QQuickWindow *m_window = nullptr;
};
