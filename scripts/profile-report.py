#!/usr/bin/env python3
"""Summarize ClipCut JSONL diagnostics; ignore warm-up and idle UI intervals."""
import argparse,json,statistics
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('logs',nargs='+',type=Path)
parser.add_argument('--warmup-ms',type=int,default=5000)
args=parser.parse_args()
def percentile(values,p):
    values=sorted(values)
    return round(values[min(len(values)-1,int((len(values)-1)*p))],2) if values else None
for path in args.logs:
    events=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    samples=[e for e in events if e.get('event')=='sample' and e.get('playing') and e.get('elapsed_ms',0)>=args.warmup_ms and e.get('visible',True) and e.get('exposed',True) and e.get('ui_frames',0)>=40]
    seeks=[e for e in events if e.get('event')=='seek_complete']
    memory=[e['memory_bytes']/1048576 for e in samples if e.get('memory_bytes',-1)>=0]
    result={'log':str(path),'startup':next((e for e in events if e.get('event')=='startup'),{}),'active_samples':len(samples),'rss_MiB_min_max':[round(min(memory),1),round(max(memory),1)] if memory else [],'UI_ms_median_p50':percentile([e['ui_frame_ms_p50'] for e in samples if 'ui_frame_ms_p50' in e],.5),'UI_ms_p95_of_sample_p95':percentile([e['ui_frame_ms_p95'] for e in samples if 'ui_frame_ms_p95' in e],.95),'CPU_one_core_percent_median':percentile([e['cpu_percent_one_core'] for e in samples if 'cpu_percent_one_core' in e],.5),'seek_count':len(seeks),'seek_ms_p50_p95':[percentile([e['latency_ms'] for e in seeks],p) for p in [.5,.95]],'errors':[e for e in events if e.get('event')=='error']}
    print(json.dumps(result,indent=2))
