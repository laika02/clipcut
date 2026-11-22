#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <cmath>
#include <cstring>
#include "core/Edit.hpp"
#include "export/Export.hpp"
#include "media/AudioMute.hpp"
using namespace clipcut;

class MuteTest:public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    Source source;
    void exportFile(const Edits &edits,const ExportOptions &options,const QString &name) {
        auto plan=planExport(source,edits,options);QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error));
        ExportJob job;QSignalSpy finished(&job,&ExportJob::finished);job.start(plan,directory.filePath(name));
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(),90000);
        QVERIFY2(finished.first()[0].toBool(),qPrintable(finished.first()[2].toString()));
    }
    QByteArray audio(const QString &path,int track) {
        return runTool(mediaTool("ffmpeg"),{"-v","error","-i",path,"-map",QString("0:a:%1").arg(track),"-ar","48000","-ac","2","-f","f32le","pipe:1"});
    }
    double rms(const QByteArray &data,double from,double to) {
        const auto first=qRound64(from*48000)*2,last=std::min<qint64>(data.size()/sizeof(float),qRound64(to*48000)*2);
        double total=0;for(qint64 i=first;i<last;++i){float value;std::memcpy(&value,data.constData()+i*sizeof(float),sizeof(float));total+=value*value;}
        return std::sqrt(total/std::max<qint64>(1,last-first));
    }
    double tone(const QByteArray &data,double from,double to,double frequency) {
        const auto first=qRound64(from*48000),last=std::min<qint64>(data.size()/(2*sizeof(float)),qRound64(to*48000));
        double real=0,imaginary=0;for(qint64 i=first;i<last;++i){float value;std::memcpy(&value,data.constData()+i*2*sizeof(float),sizeof(float));const double phase=2*3.141592653589793*frequency*i/48000;real+=value*std::cos(phase);imaginary+=value*std::sin(phase);}
        return 2*std::hypot(real,imaginary)/std::max<qint64>(1,last-first);
    }
    QByteArray packetHashes(const QString &path,const QString &stream) {
        return runTool(mediaTool("ffprobe"),{"-v","error","-select_streams",stream,"-show_packets","-show_entries","packet=data_hash","-show_data_hash","sha256","-of","csv=p=0",path});
    }
private slots:
    void initTestCase() {
        gst_init(nullptr,nullptr);QVERIFY(directory.isValid());
        const auto path=directory.filePath("tones.mkv");
        runTool(mediaTool("ffmpeg"),{"-v","error","-y","-f","lavfi","-i","color=size=96x54:rate=30:duration=4","-f","lavfi","-i","sine=frequency=440:sample_rate=48000:duration=4","-f","lavfi","-i","sine=frequency=880:sample_rate=48000:duration=4","-map","0:v","-map","1:a","-map","2:a","-c:v","libx264","-threads","2","-c:a","pcm_f32le",path});
        source=probeSource(path);
    }
    void plansAndHistory() {
        auto edits=initialEdits(source);edits.collapse=false;edits.mutes[0]={{fromSeconds(.8),fromSeconds(1.4)}};
        auto plan=planExport(source,edits,{});QVERIFY(plan.copyVideo);QVERIFY(!plan.copyAudio);QCOMPARE(plan.copiedAudio,QList<int>{1});QCOMPARE(plan.duration,source.duration);
        auto original=ExportOptions{};original.extractTrack=0;original.original=true;
        plan=planExport(source,edits,original);QVERIFY(plan.copyAudio);QVERIFY(plan.edits.mutes.isEmpty());
        edits.included={1};QVERIFY(planExport(source,edits,{}).copyAudio);
        edits.included={0};edits.deletes={{fromSeconds(.7),fromSeconds(1.5)}};
        // Mutes wholly inside deleted content have no remaining effect.
        QCOMPARE(muted(edits,0,{fromSeconds(1.5),source.duration}),QList<Interval>{});
        edits.mutes[0].append({fromSeconds(1.1),fromSeconds(1.8)});
        QCOMPARE(muted(edits,0,{fromSeconds(1.2),fromSeconds(1.6)}),(QList<Interval>{{fromSeconds(1.2),fromSeconds(1.6)}}));
        History history;auto initial=initialEdits(source);history.reset(initial);QVERIFY(history.commit(edits));QCOMPARE(history.undo(),initial);QCOMPARE(history.redo(),edits);
    }
    void sampleBoundaries() {
        std::vector<float> samples(960,1.f);
        silenceAudio(reinterpret_cast<char*>(samples.data()),480,2,48000,Second,{{Second+1000001,Second+3000001}});
        for(int i=0;i<480;++i)for(int ch=0;ch<2;++ch)QCOMPARE(samples[i*2+ch],i>=49&&i<145?0.f:1.f);
    }
    void editableMerging() {
        QList<Interval> regions{{30,40},{10,20},{18,35}};
        QCOMPARE(mergeRegions(regions,2),0);QCOMPARE(regions,(QList<Interval>{{10,40}}));
        regions={{10,20},{30,40},{20,30}};
        QCOMPARE(mergeRegions(regions,0),0);QCOMPARE(regions,(QList<Interval>{{10,40}}));
        regions={{10,40},{20,25}};
        QCOMPARE(mergeRegions(regions,1),0);QCOMPARE(regions,(QList<Interval>{{10,40}}));
        regions={{10,20},{15,15},{30,40}};
        QCOMPARE(mergeRegions(regions,1),1);QCOMPARE(regions,(QList<Interval>{{10,20},{15,15},{30,40}}));
        regions[1]={15,35};QCOMPARE(mergeRegions(regions,1),0);QCOMPARE(regions,(QList<Interval>{{10,40}}));
        History history;Edits before;before.deletes={{10,20},{30,40}};history.reset(before);
        auto after=before;after.deletes[0].end=35;mergeRegions(after.deletes);QVERIFY(history.commit(after));QCOMPARE(history.undo(),before);QCOMPARE(history.redo(),after);
    }
    void pipelineSegmentsAndSnapshots() {
        auto state=std::make_shared<AudioMuteState>();auto map=std::make_shared<TrackMutes>();(*map)[0]={{fromSeconds(2.003),fromSeconds(2.006)}};state->regions.store(map);
        auto filter=createAudioMute(state,0);gst_object_ref_sink(filter);
        auto src=gst_pad_new("source",GST_PAD_SRC),sink=gst_pad_new("capture",GST_PAD_SINK);
        QByteArray captured;gst_pad_set_element_private(sink,&captured);
        gst_pad_set_chain_function(sink,+[](GstPad *pad,GstObject*,GstBuffer *buffer)->GstFlowReturn{auto output=static_cast<QByteArray*>(gst_pad_get_element_private(pad));GstMapInfo info;if(!gst_buffer_map(buffer,&info,GST_MAP_READ))return GST_FLOW_ERROR;*output=QByteArray(reinterpret_cast<const char*>(info.data),info.size);gst_buffer_unmap(buffer,&info);gst_buffer_unref(buffer);return GST_FLOW_OK;});
        auto input=gst_element_get_static_pad(filter,"sink"),output=gst_element_get_static_pad(filter,"src");
        gst_pad_set_active(src,TRUE);gst_pad_set_active(sink,TRUE);QCOMPARE(gst_pad_link(src,input),GST_PAD_LINK_OK);QCOMPARE(gst_pad_link(output,sink),GST_PAD_LINK_OK);
        gst_element_set_state(filter,GST_STATE_PLAYING);gst_pad_push_event(src,gst_event_new_stream_start("mute-test"));
        auto caps=gst_caps_from_string("audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved");gst_pad_push_event(src,gst_event_new_caps(caps));gst_caps_unref(caps);
        GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);segment.start=20*GST_SECOND;segment.time=2*GST_SECOND;gst_pad_push_event(src,gst_event_new_segment(&segment));
        auto buffer=gst_buffer_new_allocate(nullptr,960*sizeof(float),nullptr);std::vector<float> ones(960,1.f);gst_buffer_fill(buffer,0,ones.data(),ones.size()*sizeof(float));GST_BUFFER_PTS(buffer)=20*GST_SECOND;GST_BUFFER_DURATION(buffer)=10*GST_MSECOND;
        auto shared=gst_buffer_ref(buffer);QCOMPARE(gst_pad_push(src,buffer),GST_FLOW_OK);
        for(int i=0;i<480;++i){float value;std::memcpy(&value,captured.constData()+i*2*sizeof(float),sizeof(float));QCOMPARE(value,i>=144&&i<288?0.f:1.f);}
        GstMapInfo info;gst_buffer_map(shared,&info,GST_MAP_READ);QCOMPARE(std::memcmp(info.data,ones.data(),info.size),0);gst_buffer_unmap(shared,&info);gst_buffer_unref(shared);
        state->regions.store(std::make_shared<const TrackMutes>());
        buffer=gst_buffer_new_allocate(nullptr,ones.size()*sizeof(float),nullptr);gst_buffer_fill(buffer,0,ones.data(),ones.size()*sizeof(float));GST_BUFFER_PTS(buffer)=20*GST_SECOND;
        QCOMPARE(gst_pad_push(src,buffer),GST_FLOW_OK);QCOMPARE(captured,QByteArray(reinterpret_cast<char*>(ones.data()),ones.size()*sizeof(float)));
        gst_element_set_state(filter,GST_STATE_NULL);gst_pad_unlink(src,input);gst_pad_unlink(output,sink);gst_pad_set_active(src,FALSE);gst_pad_set_active(sink,FALSE);
        for(auto pad:{src,sink,input,output})gst_object_unref(pad);gst_object_unref(filter);
    }
    void exports() {
        auto edits=initialEdits(source);edits.collapse=false;edits.mutes[0]={{fromSeconds(.8),fromSeconds(1.4)}};
        exportFile(edits,{},"separate.mkv");
        auto path=directory.filePath("separate.mkv");auto first=audio(path,0),second=audio(path,1);
        QVERIFY(rms(first,.9,1.3)<.0001);QVERIFY(rms(first,.2,.6)>.03);QVERIFY(rms(second,.9,1.3)>.03);
        QCOMPARE(packetHashes(path,"a:1"),packetHashes(source.path,"a:1"));QCOMPARE(packetHashes(path,"v:0"),packetHashes(source.path,"v:0"));
        edits.collapse=true;edits.mutes[1]={{fromSeconds(2.2),fromSeconds(2.8)}};exportFile(edits,{},"mix.mp4");auto mixed=audio(directory.filePath("mix.mp4"),0);
        QVERIFY(tone(mixed,.9,1.3,440)<.0003);QVERIFY(tone(mixed,.9,1.3,880)>.02);QVERIFY(tone(mixed,2.3,2.7,880)<.0003);QVERIFY(tone(mixed,2.3,2.7,440)>.02);
        edits.included={0};edits.trim={fromSeconds(.2),fromSeconds(3.8)};edits.deletes={{fromSeconds(1.5),fromSeconds(2)}};edits.mutes[0]={{fromSeconds(.8),fromSeconds(2.5)}};
        ExportOptions extract;extract.extractTrack=0;exportFile(edits,extract,"modified.m4a");auto modified=audio(directory.filePath("modified.m4a"),0);
        QVERIFY(rms(modified,.75,1.65)<.0001);QVERIFY(rms(modified,.1,.4)>.03);QVERIFY(rms(modified,2,2.5)>.03);
        extract.original=true;exportFile(edits,extract,"original.mka");QCOMPARE(packetHashes(directory.filePath("original.mka"),"a:0"),packetHashes(source.path,"a:0"));
        ExportOptions compress;compress.compress=true;compress.targetMB=1;exportFile(edits,compress,"compressed.mp4");auto compressed=audio(directory.filePath("compressed.mp4"),0);QVERIFY(rms(compressed,.75,1.65)<.0001);QVERIFY(QFileInfo(directory.filePath("compressed.mp4")).size()<=1000000);
    }
};
QTEST_GUILESS_MAIN(MuteTest)
#include "MuteTest.moc"
