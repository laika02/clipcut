#include "AudioMute.hpp"
#include <gst/base/gstbasetransform.h>
#include <algorithm>
#include <cstring>

void silenceAudio(char *data,size_t frames,int channels,int rate,clipcut::Time start,const QList<clipcut::Interval> &regions) {
    const auto end=start+clipcut::Time(gst_util_uint64_scale_ceil(frames,GST_SECOND,rate));
    for(auto r:regions) {
        if(r.end<=start)continue;
        if(r.start>=end)break;
        const auto first=std::min<size_t>(frames,gst_util_uint64_scale_ceil(std::max(clipcut::Time(0),r.start-start),rate,GST_SECOND));
        const auto last=std::min<size_t>(frames,gst_util_uint64_scale_ceil(std::max(clipcut::Time(0),r.end-start),rate,GST_SECOND));
        if(last>first)std::memset(data+first*channels*sizeof(float),0,(last-first)*channels*sizeof(float));
    }
}

typedef struct {GstBaseTransform parent;std::shared_ptr<AudioMuteState> *state;int track;} ClipCutMute;
typedef struct {GstBaseTransformClass parent;} ClipCutMuteClass;
G_DEFINE_TYPE(ClipCutMute,clipcut_mute,GST_TYPE_BASE_TRANSFORM)

static GstFlowReturn transformAudio(GstBaseTransform *base,GstBuffer *buffer) {
    auto self=reinterpret_cast<ClipCutMute*>(base);
    if(!self->state||!GST_BUFFER_PTS_IS_VALID(buffer))return GST_FLOW_OK;
    const auto snapshot=(*self->state)->regions.load();
    const auto found=snapshot->constFind(self->track);
    if(found==snapshot->constEnd()||found->isEmpty())return GST_FLOW_OK;
    const auto time=gst_segment_to_stream_time(&base->segment,GST_FORMAT_TIME,GST_BUFFER_PTS(buffer));
    if(time==GST_CLOCK_TIME_NONE)return GST_FLOW_OK;
    const auto frames=gst_buffer_get_size(buffer)/(2*sizeof(float));
    const auto end=time+gst_util_uint64_scale_ceil(frames,GST_SECOND,48000);
    if(std::none_of(found->cbegin(),found->cend(),[&](auto r){return r.end>clipcut::Time(time)&&r.start<clipcut::Time(end);}))return GST_FLOW_OK;
    GstMapInfo map;
    if(!gst_buffer_map(buffer,&map,GST_MAP_WRITE))return GST_FLOW_ERROR;
    silenceAudio(reinterpret_cast<char*>(map.data),frames,2,48000,clipcut::Time(time),*found);
    gst_buffer_unmap(buffer,&map);return GST_FLOW_OK;
}
static void clipcut_mute_class_init(ClipCutMuteClass *klass) {
    auto object=G_OBJECT_CLASS(klass);
    object->finalize=[](GObject *object){auto self=reinterpret_cast<ClipCutMute*>(object);delete self->state;G_OBJECT_CLASS(clipcut_mute_parent_class)->finalize(object);};
    auto element=GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(element,"ClipCut track mute","Filter/Effect/Audio","Sample-accurate source-time track mutes","ClipCut");
    auto caps=gst_caps_from_string("audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved");
    gst_element_class_add_pad_template(element,gst_pad_template_new("sink",GST_PAD_SINK,GST_PAD_ALWAYS,caps));
    gst_element_class_add_pad_template(element,gst_pad_template_new("src",GST_PAD_SRC,GST_PAD_ALWAYS,caps));gst_caps_unref(caps);
    GST_BASE_TRANSFORM_CLASS(klass)->transform_ip=transformAudio;
}
static void clipcut_mute_init(ClipCutMute *self) {self->state=nullptr;gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self),TRUE);}
GstElement *createAudioMute(const std::shared_ptr<AudioMuteState> &state,int track) {
    auto self=reinterpret_cast<ClipCutMute*>(g_object_new(clipcut_mute_get_type(),nullptr));
    self->state=new std::shared_ptr<AudioMuteState>(state);self->track=track;return GST_ELEMENT(self);
}
