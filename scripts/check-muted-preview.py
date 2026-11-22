#!/usr/bin/env python3
"""Check --exercise-mutes capture from the audible eight-track test fixture."""
import array
import json
import math
import pathlib
import sys

samples = array.array('f')
samples.frombytes(pathlib.Path(sys.argv[1]).read_bytes())
if sys.byteorder != 'little':
    samples.byteswap()

def rms(start, end):
    window = samples[round(start * 48000) * 2:round(end * 48000) * 2]
    return math.sqrt(sum(value * value for value in window) / max(1, len(window)))

report = {
    'duration_seconds': len(samples) / (2 * 48000),
    'muted_rms': rms(.9, 1.3),
    'before_rms': rms(.2, .6),
    'after_rms': rms(1.6, 2.0),
}
report['success'] = (report['duration_seconds'] > 2 and report['muted_rms'] < .0001
                     and report['before_rms'] > .02 and report['after_rms'] > .02)
print(json.dumps(report, indent=2))
sys.exit(0 if report['success'] else 1)
