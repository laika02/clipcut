#include "Preview.hpp"
#include <QQuickWindow>
#include <QRunnable>
#include <QDateTime>
#include <QUrl>
#include <QDebug>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
class RenderJob : public QRunnable {
public:
    explicit RenderJob(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void run() override { m_fn(); }
private: std::function<void()> m_fn;
};

PlaybackWorker::PlaybackWorker(GstElement *sink) : m_sink(GST_ELEMENT(gst_object_ref(sink))) {
    gst_segment_init(&m_videoSegment, GST_FORMAT_TIME);
}
PlaybackWorker::~PlaybackWorker() { shutdown(); gst_object_unref(m_sink); }
void PlaybackWorker::shutdown() {
    ++m_generation;
    if (m_timer) m_timer->stop();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        if(m_probe) { auto pad=gst_element_get_static_pad(m_sink,"sink");gst_pad_remove_probe(pad,m_probe);gst_object_unref(pad);m_probe=0; }
        // Keep the scene's sink alive across loads; the bin owns its own reference.
        gst_bin_remove(GST_BIN(m_pipeline), m_sink);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    gst_element_set_state(m_sink,GST_STATE_NULL);
    if (m_bus) { gst_object_unref(m_bus); m_bus = nullptr; }
    m_audio.clear(); m_audioCount = 0; m_playing = false; m_seeking = false; m_pending = false;
    m_started = false; m_segment = -1; m_latestFrame = GST_CLOCK_TIME_NONE;
    m_videoQueue = nullptr; m_mixer = nullptr;
    m_buffers=0;
    m_lastMutes.clear();m_mutes->regions.store(std::make_shared<const TrackMutes>());
}
void PlaybackWorker::open(const QString &path,const QList<int> &ids) {
    shutdown();
    m_containerIds=ids;
    m_pipeline = gst_pipeline_new("clipcut-preview");
    GstElement *source = gst_element_factory_make("uridecodebin", "source");
    m_videoQueue = gst_element_factory_make("queue", "video-queue");
    m_mixer = gst_element_factory_make("audiomixer", "mix");
    GstElement *audioConvert = gst_element_factory_make("audioconvert", nullptr);
    const auto capture=qEnvironmentVariable("CLIPCUT_CAPTURE_AUDIO");
    GstElement *audioSink = gst_element_factory_make(!capture.isEmpty()?"filesink":qEnvironmentVariableIsSet("CLIPCUT_SILENT") ? "fakesink" : "autoaudiosink", "audio-output");
#ifdef Q_OS_WIN
    GstElement *upload = gst_element_factory_make("d3d11upload", nullptr);
    GstElement *convert = gst_element_factory_make("d3d11convert", nullptr);
    GstElement *compat = gst_element_factory_make("capsfilter", nullptr);
    if(qEnvironmentVariableIsSet("CLIPCUT_WINE_COMPAT")) {
        auto caps=gst_caps_from_string("video/x-raw(memory:D3D11Memory),format=BGRA");g_object_set(compat,"caps",caps,nullptr);gst_caps_unref(caps);
    }
#else
    GstElement *upload = gst_element_factory_make("glupload", nullptr);
    GstElement *convert = gst_element_factory_make("glcolorconvert", nullptr);
    GstElement *compat = gst_element_factory_make("capsfilter", nullptr);
#endif
    if (!source || !m_videoQueue || !m_mixer || !audioConvert || !audioSink || !upload || !convert || !compat) {
        for (auto e : {source, m_videoQueue, m_mixer, audioConvert, audioSink, upload, convert, compat}) if (e) gst_object_unref(e);
        m_videoQueue = nullptr; m_mixer = nullptr;
        emit failure("A required GStreamer preview plugin is missing. Run the dependency setup/package check.");
        shutdown(); return;
    }
    g_object_set(source, "uri", QUrl::fromLocalFile(path).toEncoded().constData(), nullptr);
    g_object_set(m_videoQueue, "max-size-buffers", 4u, "max-size-bytes", 0u, "max-size-time", guint64(0), nullptr);
    if (qEnvironmentVariableIsSet("CLIPCUT_SILENT")) g_object_set(audioSink, "sync", TRUE, nullptr);
    if(!capture.isEmpty())g_object_set(audioSink,"location",capture.toUtf8().constData(),"sync",TRUE,nullptr);
    gst_bin_add_many(GST_BIN(m_pipeline), source, m_videoQueue, upload, convert,
                     compat, GST_ELEMENT(gst_object_ref(m_sink)), m_mixer, audioConvert, audioSink, nullptr);
    if (!gst_element_link_many(m_videoQueue, upload, convert, compat, m_sink, nullptr) ||
        !gst_element_link_many(m_mixer, audioConvert, audioSink, nullptr)) {
        emit failure("Could not link GPU preview or audio output."); shutdown(); return;
    }
    g_signal_connect(source, "pad-added", G_CALLBACK(padAdded), this);
    GstPad *videoPad = gst_element_get_static_pad(m_sink, "sink");
    m_probe=gst_pad_add_probe(videoPad, GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM), observeFrame, this, nullptr);
    gst_object_unref(videoPad);
    m_bus = gst_element_get_bus(m_pipeline);
    if (!m_timer) { m_timer = new QTimer(this); connect(m_timer, &QTimer::timeout, this, &PlaybackWorker::poll); }
    m_timer->start(20);
    // The Qt sink must propagate its display/device before any upload/decoder.
    gst_element_set_state(m_sink, GST_STATE_READY);
    if (auto context = gst_element_get_context(m_sink, "gst.gl.GLDisplay")) {
        gst_element_set_context(m_pipeline, context); gst_context_unref(context);
    }
    if (gst_element_set_state(m_pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
        emit failure("Preview initialization failed; see Settings → Diagnostics.");
    emit playing(false);
}
void PlaybackWorker::padAdded(GstElement *, GstPad *pad, gpointer user) {
    auto self = static_cast<PlaybackWorker *>(user);
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const char *type = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    if (g_str_has_prefix(type, "video/")) {
        GstPad *target = gst_element_get_static_pad(self->m_videoQueue, "sink");
        if (!gst_pad_is_linked(target)) gst_pad_link(pad, target);
        gst_object_unref(target);
    } else if (g_str_has_prefix(type, "audio/")) {
        const int arrival = self->m_audioCount.fetch_add(1);
        const auto generation=self->m_generation.load();
        auto streamId=gst_pad_get_stream_id(pad);
        const QString suffix=QString::fromUtf8(streamId?streamId:"").section('/',-1).section(':',0,0);
        bool known=false;int number=suffix.toInt(&known);int index=known?self->m_containerIds.indexOf(number):-1;
        if(index<0)index=arrival;
        qInfo()<<"Audio identity"<<index<<streamId<<"container mapping"<<(known&&self->m_containerIds.contains(number));
        g_free(streamId);
        auto queue = gst_element_factory_make("queue", nullptr);
        auto convert = gst_element_factory_make("audioconvert", nullptr);
        auto resample = gst_element_factory_make("audioresample", nullptr);
        auto filter = gst_element_factory_make("capsfilter", nullptr);
        auto volume = gst_element_factory_make("volume", nullptr);
        auto mute=createAudioMute(self->m_mutes,index);
        if (!queue || !convert || !resample || !filter || !volume || !mute) { gst_caps_unref(caps); return; }
        g_object_set(queue, "max-size-buffers", 0u, "max-size-bytes", 0u, "max-size-time", guint64(250 * GST_MSECOND), nullptr);
        GstCaps *audioCaps = gst_caps_from_string("audio/x-raw,rate=48000,format=F32LE,channels=2,layout=interleaved");
        g_object_set(filter, "caps", audioCaps, nullptr); gst_caps_unref(audioCaps);
        gst_bin_add_many(GST_BIN(self->m_pipeline), queue, convert, resample, filter, volume, mute, nullptr);
        gst_element_link_many(queue, convert, resample, filter, volume, mute, self->m_mixer, nullptr);
        GstPad *target = gst_element_get_static_pad(queue, "sink"); gst_pad_link(pad, target); gst_object_unref(target);
        // Streaming owns branch construction; publish routing on the control thread.
        gst_object_ref(volume);
        QMetaObject::invokeMethod(self, [self, volume, index, generation] {
            if(generation==self->m_generation) { self->m_audio.append({volume, index}); self->route(); emit self->audioTracks(self->m_audio.size()); }
            gst_object_unref(volume);
        }, Qt::QueuedConnection);
        for (auto e : {queue, convert, resample, filter, volume, mute}) gst_element_sync_state_with_parent(e);
    }
    gst_caps_unref(caps);
}
GstPadProbeReturn PlaybackWorker::observeFrame(GstPad *, GstPadProbeInfo *info, gpointer user) {
    auto self = static_cast<PlaybackWorker *>(user);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        auto event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment *segment; gst_event_parse_segment(event, &segment); self->m_videoSegment = *segment;
        }
    } else if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
        auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        ++self->m_buffers;
        if (GST_BUFFER_PTS_IS_VALID(buffer)) self->m_latestFrame = gst_segment_to_stream_time(&self->m_videoSegment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
    }
    return GST_PAD_PROBE_OK;
}
void PlaybackWorker::setPlaying(bool value) {
    if (!m_pipeline) return;
    m_playing = value;
    if (value && !m_ranges.isEmpty()) {
        gint64 current = 0; gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &current);
        const double seconds = double(current) / GST_SECOND;
        m_segment = -1;
        for (int i = 0; i < m_ranges.size(); ++i) {
            auto r = m_ranges[i].toMap();
            if (seconds < r["end"].toDouble()) { m_segment = i; break; }
        }
        if (m_segment < 0) m_segment = 0;
        auto r = m_ranges[m_segment].toMap();
        m_target = std::clamp(seconds, r["start"].toDouble(), r["end"].toDouble());
        m_exact = true; m_pending = true; dispatchSeek();
    }
    gst_element_set_state(m_pipeline, value ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    emit playing(value);
}
void PlaybackWorker::requestSeek(double seconds, bool exact) {
    m_target = std::max(0.0, seconds); m_exact = exact; m_pending = true;
    // Inspecting excluded areas is permitted; enforce kept sections again on play.
    m_segment = -1;
    dispatchSeek();
}
void PlaybackWorker::dispatchSeek() {
    if (!m_pipeline || !m_started || m_seeking || !m_pending) return;
    m_pending = false; m_seeking = true; m_seekStarted = nowMs();
    m_dispatchedExact=m_exact;
    auto flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH | (m_exact ? GST_SEEK_FLAG_ACCURATE : GST_SEEK_FLAG_KEY_UNIT));
    GstSeekType stopType = GST_SEEK_TYPE_NONE; gint64 stop = GST_CLOCK_TIME_NONE;
    if (m_playing && m_segment >= 0 && m_segment < m_ranges.size()) {
        flags = GstSeekFlags(flags | GST_SEEK_FLAG_SEGMENT);
        stopType = GST_SEEK_TYPE_SET;
        stop = qint64(m_ranges[m_segment].toMap()["end"].toDouble() * GST_SECOND);
    }
    if (!gst_element_seek(m_pipeline, 1.0, GST_FORMAT_TIME, flags,
            GST_SEEK_TYPE_SET, qint64(m_target * GST_SECOND), stopType, stop)) {
        m_seeking = false; emit failure("This source could not seek to the requested position.");
    }
}
void PlaybackWorker::setRouting(int solo, const QList<int> &included) { m_solo = solo; m_included = included; route(); }
void PlaybackWorker::route() {
    for (auto b : m_audio) {
        bool enabled = m_solo >= 0 ? b.index == m_solo : m_included.contains(b.index);
        // Matching export uses fixed headroom: sum tracks at 1/N to avoid clipping.
        double gain = m_solo >= 0 ? 1.0 : 1.0 / std::max(1, int(m_included.size()));
        g_object_set(b.volume, "mute", !enabled, "volume", gain, nullptr);
    }
}
void PlaybackWorker::setRanges(const QVariantList &ranges) { m_ranges = ranges; }
void PlaybackWorker::setMutes(const TrackMutes &mutes) {
    if(mutes==m_lastMutes)return;
    m_lastMutes=mutes;auto normalized=std::make_shared<TrackMutes>();
    for(auto it=mutes.cbegin();it!=mutes.cend();++it)normalized->insert(it.key(),clipcut::normalize(it.value()));
    m_mutes->regions.store(normalized);
}
void PlaybackWorker::poll() {
    if (!m_pipeline || !m_bus) return;
    while (GstMessage *message = gst_bus_pop(m_bus)) {
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *error = nullptr; gchar *debug = nullptr; gst_message_parse_error(message, &error, &debug);
            QString detail = QString::fromUtf8(error->message) + "\n" + QString::fromUtf8(debug ? debug : "");
            emit failure(detail); g_error_free(error); g_free(debug);
            m_playing = false; emit playing(false); break;
        }
        case GST_MESSAGE_ASYNC_DONE: {
            const bool initial = !m_started; m_started = true;
            if (m_seeking) { m_seeking = false; emit seekFinished(nowMs() - m_seekStarted, m_dispatchedExact); }
            if (initial) {
                gint64 length = 0; if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &length)) emit duration(double(length) / GST_SECOND);
                GstPad *pad = gst_element_get_static_pad(m_sink, "sink");
                GstCaps *caps = gst_pad_get_current_caps(pad);
                auto text = caps ? gst_caps_to_string(caps) : g_strdup("no caps");
                QString detail = QString("%1 · %2").arg(GST_OBJECT_NAME(m_sink), QString::fromUtf8(text));
                g_free(text); if (caps) gst_caps_unref(caps); gst_object_unref(pad);
                GstIterator *it = gst_bin_iterate_recurse(GST_BIN(m_pipeline)); GValue value = G_VALUE_INIT;
                while (gst_iterator_next(it, &value) == GST_ITERATOR_OK) {
                    auto e = GST_ELEMENT(g_value_get_object(&value)); auto factory = gst_element_get_factory(e);
                    if (factory && gst_element_factory_list_is_type(factory, GST_ELEMENT_FACTORY_TYPE_DECODER))
                        detail += "\nDecoder: " + QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
                    g_value_reset(&value);
                }
                g_value_unset(&value); gst_iterator_free(it); emit diagnostics(detail);
            }
            dispatchSeek(); break;
        }
        case GST_MESSAGE_SEGMENT_DONE:
            if (m_playing && ++m_segment < m_ranges.size()) {
                m_target = m_ranges[m_segment].toMap()["start"].toDouble(); m_exact = true; m_pending = true;
                m_seeking = false; dispatchSeek();
            } else setPlaying(false);
            break;
        case GST_MESSAGE_EOS: setPlaying(false); break;
        default: break;
        }
        gst_message_unref(message);
    }
    gint64 value = 0; if (!m_seeking && gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &value)) emit position(double(value) / GST_SECOND);
    const auto pts = m_latestFrame.load(); if (pts != GST_CLOCK_TIME_NONE) emit frame(double(pts) / GST_SECOND);
    if (m_seeking && nowMs() - m_seekStarted > 5000) { m_seeking = false; emit failure("Seek timed out; the latest target can be retried."); dispatchSeek(); }
    if (nowMs() - m_lastSample > 1000) {
        m_lastSample = nowMs(); GstStructure *stats = nullptr;
        g_object_get(m_sink, "stats", &stats, nullptr);
        if (stats) {
            guint64 rendered = 0, dropped = 0;
            gst_structure_get_uint64(stats, "rendered", &rendered); gst_structure_get_uint64(stats, "dropped", &dropped);
            emit performanceSample({{"sink_rendered", qint64(rendered)}, {"sink_dropped", qint64(dropped)}, {"delivered_buffers", qint64(m_buffers.load())}, {"seek_in_flight", m_seeking}, {"pending_target", m_pending}});
            gst_structure_free(stats);
        }
    }
}

Preview::Preview(QObject *parent) : QObject(parent) {
#ifdef Q_OS_WIN
    // Wine's shared planar textures can fail in DXVK; keep real Windows on native caps.
    if(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version") && !qEnvironmentVariableIsSet("CLIPCUT_WINE_COMPAT"))qputenv("CLIPCUT_WINE_COMPAT","1");
    m_sink = gst_element_factory_make("qml6d3d11sink", "qt-video");
#else
    m_sink = gst_element_factory_make("qml6glsink", "qt-video");
#endif
    if (!m_sink) m_error = "The Qt GStreamer video sink is unavailable. Build the matching plugin before running ClipCut.";
    else { gst_object_ref_sink(m_sink);g_object_set(m_sink,"qos",TRUE,nullptr); }
}
Preview::~Preview() {
    stop();
    if (m_sink) gst_object_unref(m_sink);
}
void Preview::stop() {
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, &PlaybackWorker::shutdown, Qt::BlockingQueuedConnection);
        m_thread.quit(); m_thread.wait();
        m_worker=nullptr;
    }
}
void Preview::attach(QQuickItem *item) {
    if (!m_sink || !item || !item->window() || m_ready) return;
    g_object_set(m_sink, "widget", item, nullptr);
    item->window()->scheduleRenderJob(new RenderJob([this] {
        const auto result = gst_element_set_state(m_sink, GST_STATE_READY);
        QMetaObject::invokeMethod(this, [this, result] {
            if (result == GST_STATE_CHANGE_FAILURE) { m_error = "Qt/GStreamer graphics context initialization failed."; emit errorChanged(); return; }
            startWorker();
        });
    }), QQuickWindow::BeforeSynchronizingStage);
    item->window()->update();
}
void Preview::startWorker() {
    m_worker = new PlaybackWorker(m_sink); m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &PlaybackWorker::position, this, [this](double value) { m_position = value; emit positionChanged(); });
    connect(m_worker, &PlaybackWorker::frame, this, [this](double value) { m_presented = value; emit presentedChanged(); });
    connect(m_worker, &PlaybackWorker::duration, this, [this](double value) { m_duration = value; emit durationChanged(); });
    connect(m_worker, &PlaybackWorker::playing, this, [this](bool value) { m_playing = value; emit playingChanged(); });
    connect(m_worker, &PlaybackWorker::diagnostics, this, [this](QString value) { m_diagnostics = value; qInfo().noquote() << value; emit diagnosticsChanged(); });
    connect(m_worker, &PlaybackWorker::failure, this, [this](QString value) { m_error = value; qWarning().noquote() << value; emit errorChanged(); });
    connect(m_worker, &PlaybackWorker::seekFinished, this, &Preview::seekMeasured);
    connect(m_worker, &PlaybackWorker::audioTracks, this, &Preview::tracksChanged);
    connect(m_worker, &PlaybackWorker::performanceSample, this, &Preview::performanceSample);
    m_thread.start(); m_ready = true; emit readyChanged();
    if (!m_pendingPath.isEmpty()) open(m_pendingPath,m_pendingIds);
}
void Preview::open(const QString &path,const QVariantList &ids) {
    if (!m_ready) { m_pendingPath = path;m_pendingIds=ids; return; }
    const QString requestedPath = path;
    QList<int> containerIds;for(auto id:ids)containerIds.append(id.toInt());
    m_pendingPath.clear(); m_error.clear(); emit errorChanged();
    m_position=0;m_presented=0;m_duration=0;emit positionChanged();emit durationChanged();
    QMetaObject::invokeMethod(m_worker, [this, requestedPath,containerIds] { m_worker->open(requestedPath,containerIds); });
}
void Preview::play(bool value) { if (m_worker) QMetaObject::invokeMethod(m_worker, [this, value] { m_worker->setPlaying(value); }); }
void Preview::toggle() { play(!m_playing); }
void Preview::seek(double seconds, bool exact) {
    m_position = seconds; emit positionChanged();
    if (m_worker) QMetaObject::invokeMethod(m_worker, [this, seconds, exact] { m_worker->requestSeek(seconds, exact); });
}
void Preview::route(int solo, const QVariantList &included) {
    QList<int> indices; for (auto i : included) indices.append(i.toInt());
    if (m_worker) QMetaObject::invokeMethod(m_worker, [this, solo, indices] { m_worker->setRouting(solo, indices); });
}
void Preview::ranges(const QVariantList &ranges) { if (m_worker) QMetaObject::invokeMethod(m_worker, [this, ranges] { m_worker->setRanges(ranges); }); }
void Preview::mutes(const TrackMutes &regions) {if(m_worker)QMetaObject::invokeMethod(m_worker,[this,regions]{m_worker->setMutes(regions);});}
