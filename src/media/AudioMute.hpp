#pragma once
#include "core/Edit.hpp"
#include <gst/gst.h>
#include <atomic>
#include <memory>

using TrackMutes=QHash<int,QList<clipcut::Interval>>;
struct AudioMuteState {
    std::atomic<std::shared_ptr<const TrackMutes>> regions{std::make_shared<const TrackMutes>()};
};
// The preview negotiates interleaved F32LE stereo at 48 kHz. Silence only
// samples inside [start,end), preserving timestamps, duration and mix gain.
void silenceAudio(char *data,size_t frames,int channels,int rate,clipcut::Time start,const QList<clipcut::Interval> &regions);
GstElement *createAudioMute(const std::shared_ptr<AudioMuteState> &state,int track);
