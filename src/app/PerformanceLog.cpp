#include "PerformanceLog.hpp"
#include <QJsonDocument>
#include <QSysInfo>
#include <QDebug>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

static qint64 residentBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS info{};
    return GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)) ? info.WorkingSetSize : -1;
#else
    QFile file("/proc/self/status"); if (!file.open(QIODevice::ReadOnly)) return -1;
    for (auto line : file.readAll().split('\n')) if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong() * 1024;
    return -1;
#endif
}
static qint64 cpuMicroseconds() {
#ifdef Q_OS_WIN
    FILETIME creation,exit,kernel,user;
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))return -1;
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;return (k.QuadPart+u.QuadPart)/10;
#else
    timespec value{};if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&value))return -1;return qint64(value.tv_sec)*1000000+value.tv_nsec/1000;
#endif
}
PerformanceLog::PerformanceLog(const QString &path, Preview *preview, QQuickWindow *window) {
    m_clock.start();
    setPath(path);
    if(window) observeWindow(window);
    connect(preview, &Preview::presentedChanged, this, [this, preview] {
        if (!m_firstFrame) { m_firstFrame = true; event("first_video_buffer", {{"source_seconds", preview->presented()},{"measurement","sink input, before compositor presentation"}}); }
    });
    connect(preview, &Preview::seekMeasured, this, [this](double ms, bool exact) { event("seek_complete", {{"latency_ms", ms}, {"exact", exact}}); });
    connect(preview, &Preview::diagnosticsChanged, this, [this, preview] { event("pipeline", {{"details", preview->diagnostics()}}); });
    connect(preview, &Preview::errorChanged, this, [this, preview] { if (!preview->error().isEmpty()) event("error", {{"message", preview->error()}}); });
    connect(preview, &Preview::playingChanged, this, [this, preview] { event("transport", {{"playing", preview->playing()}}); });
    connect(preview, &Preview::performanceSample, this, [this](QJsonObject data) { event("sink", data); });
    connect(&m_timer, &QTimer::timeout, this, [this, preview] {
        std::sort(m_frames.begin(), m_frames.end());
        QJsonObject data{{"memory_bytes", residentBytes()}, {"position_seconds", preview->position()}, {"presented_seconds", preview->presented()}, {"playing", preview->playing()}, {"ui_frames", m_frames.size()}};
        if(m_window){data["visible"]=m_window->isVisible();data["exposed"]=m_window->isExposed();data["active"]=m_window->isActive();}
        data.remove("presented_seconds");data["delivered_pts_seconds"]=preview->presented();
        const auto cpu=cpuMicroseconds(),elapsed=m_clock.elapsed();
        if(m_previousSampleMs && cpu>=0 && m_previousCpu>=0)data["cpu_percent_one_core"]=double(cpu-m_previousCpu)/std::max(qint64(1),elapsed-m_previousSampleMs)/10;
        m_previousCpu=cpu;m_previousSampleMs=elapsed;
        if (!m_frames.isEmpty()) { data["ui_frame_ms_p50"] = m_frames[m_frames.size() / 2]; data["ui_frame_ms_p95"] = m_frames[std::min(m_frames.size() - 1, qsizetype(m_frames.size() * 0.95))]; }
        m_frames.clear(); event("sample", data);
    });
    m_timer.start(1000);
}
PerformanceLog::~PerformanceLog() { if (m_file.isOpen()) event("shutdown", {{"memory_bytes", residentBytes()}}); }
void PerformanceLog::observeWindow(QQuickWindow *window) {
    m_window=window;
    connect(window,&QQuickWindow::beforeSynchronizing,this,[this]{
        const auto ns=m_clock.nsecsElapsed();if(m_previousFrame)m_frames.append(double(ns-m_previousFrame)/1000000);m_previousFrame=ns;
    },Qt::QueuedConnection);
}
void PerformanceLog::setPath(const QString &value) {
    if(m_file.isOpen()) { event("logging_stopped"); m_file.close(); }
    QString file=value;
    if(value.startsWith("file:")) file=QUrl(value).toLocalFile();
    if(!file.isEmpty()) {
        m_file.setFileName(file);
        if(!m_file.open(QIODevice::WriteOnly|QIODevice::Truncate)) qWarning()<<"Could not open performance log:"<<m_file.errorString();
        else event("startup",{{"version","0.1.0"},{"qt",qVersion()},{"gstreamer",gst_version_string()},{"os",QSysInfo::prettyProductName()},{"platform",QGuiApplication::platformName()},{"memory_bytes",residentBytes()}});
    }
    m_frames.clear();m_previousFrame=0;m_firstFrame=false;emit pathChanged();
}
void PerformanceLog::event(const QString &type, QJsonObject data) {
    if (!m_file.isOpen()) return;
    data["event"] = type; data["elapsed_ms"] = m_clock.elapsed();
    m_file.write(QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n'); m_file.flush();
}
