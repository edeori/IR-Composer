#!/usr/bin/env python3
"""macOS VST3 echo regression, using WITCHMVRK output with a short attack + silence.

Requires numpy, scipy, soundfile, pedalboard. Input: 48 kHz stereo, attack ending
before 0.8 s, followed by at least 10 seconds of silence. Example:
  python Tests/VstEchoTest.py --out build/echo-investigation/new-run
Use --amp-input to reuse a previous WITCHMVRK render for an exact before/after.

Tests the built VST3 with both sibling WITCHMVRK measurement IRs, in one and four
slots. Pumps the macOS message loop: sleeping alone leaves async IR loads pending
and can produce a misleading all-silent "pass". Fresh-load crops are requested via
-1 sentinels, then their detected length is applied (no arbitrary 250 ms limit).
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

import numpy as np
from pedalboard import load_plugin
import soundfile as sf
from scipy.signal import fftconvolve, resample_poly

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000
BLOCK = 128
ENCODING = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'


def decode(data):
    size, encoded = data.split('.', 1)
    value = sum(ENCODING.index(char) << (6 * i) for i, char in enumerate(encoded))
    return value.to_bytes(int(size), 'little')


def encode(data):
    value = int.from_bytes(data, 'little')
    return str(len(data)) + '.' + ''.join(
        ENCODING[(value >> (6 * i)) & 63] for i in range((len(data) * 8 + 5) // 6))


def xml(data):
    size = struct.unpack_from('<I', data, 4)[0]
    return ET.fromstring(data[8:8 + size])


def replace_xml(data, root):
    size = struct.unpack_from('<I', data, 4)[0]
    encoded = ET.tostring(root, encoding='utf-8')
    return data[:4] + struct.pack('<I', len(encoded)) + encoded + data[8 + size:]


def get_state(plugin):
    return xml(decode(xml(plugin.raw_state).find('IComponent').text))


def set_state(plugin, state):
    data = plugin.raw_state
    wrapper = xml(data)
    component = wrapper.find('IComponent')
    component.text = encode(replace_xml(decode(component.text), state))
    plugin.raw_state = replace_xml(data, wrapper)


def process(plugin, data):
    return plugin.process(data, RATE, buffer_size=BLOCK, reset=False)


def render_amp(out):
    witch = ROOT.parent / 'WITCHMVRK-VST'
    with sf.SoundFile(witch / 'measurements/di - 25 Earth Elemental - rythm gtr 2.wav') as source:
        source.seek(6 * source.samplerate)
        audio = source.read(int(.35 * source.samplerate), dtype='float32')
        divisor = np.gcd(RATE, source.samplerate)
        audio = resample_poly(audio, RATE // divisor, source.samplerate // divisor)
    audio[-240:] *= np.linspace(1, 0, 240)
    audio = np.concatenate([np.zeros(RATE // 10), audio, np.zeros(RATE * 10)])
    di = out / 'short-di.wav'
    sf.write(di, audio, RATE, subtype='FLOAT')
    settings = out / 'external-cab.json'
    settings.write_text('{"cabinet": 0}')
    amp = out / 'amp-default.wav'
    command = [witch / 'build/Tools/WITCHMVRKPluginRender',
               witch / 'build/VST3/WITCHMVRK.vst3', di, amp,
               'sentinel', str(BLOCK), '55', '50', '58', '1', '1', '100', settings]
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(('WITCHMVRK_', 'SENTINEL_', 'WRAITH_'))}
    environment['WITCHMVRK_TEST_FULL_CHAIN'] = '1'
    result = subprocess.run([str(arg) for arg in command], env=environment,
                            capture_output=True, text=True)
    (out / 'amp-default.log').write_text(json.dumps([str(arg) for arg in command])
                                       + '\n' + result.stdout + result.stderr)
    result.check_returncode()
    return amp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--amp-input', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    if args.amp_input is None:
        args.amp_input = render_amp(out)
    cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    cf.CFRunLoopRunInMode.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_bool]
    mode = ctypes.c_void_p.in_dll(cf, 'kCFRunLoopDefaultMode')

    def settle(plugin):
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            process(plugin, np.zeros((2, BLOCK), np.float32))
            cf.CFRunLoopRunInMode(mode, .002, False)

    amp, rate = sf.read(args.amp_input, dtype='float32', always_2d=True)
    assert rate == RATE and amp.shape[1] == 2
    assert len(amp) > RATE * 10 and np.max(abs(amp[int(.8 * RATE):])) < 1e-5, \
        'Input must contain a short attack, then at least 10 seconds of silence'
    vst = ROOT / 'build/VST3/IR Composer.vst3'
    report = {'input': str(args.amp_input.resolve()), 'rows': [], 'sha256': {}}
    for path in (args.amp_input, vst / 'Contents/MacOS/IR Composer'):
        report['sha256'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    outputs = {}
    for name, file in [('calibrated', 'ML IR Color Calibrated.wav'), ('capture', 'IR.wav')]:
        source = ROOT.parent / 'WITCHMVRK-VST/measurements' / file
        report['sha256'][str(source)] = hashlib.sha256(source.read_bytes()).hexdigest()
        for slots in (1, 4):
            plugin = load_plugin(str(vst))
            plugin.process(np.zeros((2, BLOCK), np.float32), RATE, buffer_size=BLOCK)
            state = get_state(plugin)
            app = state.find('IRComposerAppState')
            for slot in range(slots):
                app.set(f'slotFilePath{slot}', str(source))
                app.set(f'slotCropStartSample{slot}', '-1')
                app.set(f'slotCropEndSample{slot}', '-1')
            app.set('combineLengthSamples', '12000')
            set_state(plugin, state)
            settle(plugin)
            state = get_state(plugin)
            app = state.find('IRComposerAppState')
            for slot in range(slots):
                assert int(app.get(f'slotCropStartSample{slot}')) >= 0, 'IR load did not complete'
                assert int(app.get(f'slotCropEndSample{slot}')) > 0, 'IR load did not complete'
            length = max(int(app.get(f'slotCropEndSample{s}'))
                         - int(app.get(f'slotCropStartSample{s}')) for s in range(slots))
            app.set('combineLengthSamples', str(length))
            set_state(plugin, state)
            settle(plugin)
            impulse = np.zeros((2, RATE * 10), np.float32)
            impulse[:, 0] = .25
            response = process(plugin, impulse).T
            assert np.max(abs(response)) > 1e-4, 'Silent response is not a valid test'
            output = process(plugin, amp.T).T
            kernel = response / .25
            expected = np.stack([fftconvolve(amp[:, c], kernel[:, c])[:len(amp)]
                                 for c in range(2)], axis=1)
            expected = np.clip(expected, -4, 4)
            assert np.isfinite(output).all() and np.isfinite(expected).all()
            sf.write(out / f'{name}-{slots}-vst.wav', output, RATE, subtype='FLOAT')
            sf.write(out / f'{name}-{slots}-impulse.wav', response, RATE, subtype='FLOAT')
            tail = output[int(.8 * RATE):]
            peak = float(np.max(abs(output)))
            assert peak > 1e-4, 'Silent guitar output is not a valid test'
            row = dict(name=name, slots=slots, length_seconds=length / RATE,
                       late_peak_db=float(20 * np.log10(max(np.max(abs(tail)) / peak, 1e-20))),
                       reference_peak_error=float(np.max(abs(output - expected))))
            row['passed'] = row['late_peak_db'] < -90 and row['reference_peak_error'] < 2e-5
            if slots == 1:
                outputs[name] = output
            else:
                row['difference_from_one_slot'] = float(np.max(abs(output - outputs[name])))
                row['passed'] &= row['difference_from_one_slot'] < 2e-5
            report['rows'].append(row)
            print(row, flush=True)
            (out / 'vst-metrics.json').write_text(json.dumps(report, indent=2))
            del plugin
    return 0 if all(row['passed'] for row in report['rows']) else 1


if __name__ == '__main__':
    raise SystemExit(main())
