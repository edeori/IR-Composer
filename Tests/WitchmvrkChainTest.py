#!/usr/bin/env python3
"""WITCHMVRK VST3 (cab/pedals/post FX off) -> IR Composer live processor.

Run with a Python environment containing numpy, scipy and soundfile:
  python Tests/WitchmvrkChainTest.py --out build/witchmvrk-chain/verified
Build IRComposerIntegrationTests first. Generated WAVs/metrics stay under build/.
The IR Composer driver uses its production processor; WITCHMVRK is hosted as VST3.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, resample_poly

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, log):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('WITCHMVRK_', 'SENTINEL_', 'WRAITH_'))}
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, env=env)
    log.write_text(json.dumps([str(x) for x in command]) + '\n' + result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--witchmvrk', type=Path, default=ROOT.parent / 'WITCHMVRK-VST')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    witch = args.witchmvrk.resolve()
    renderer = witch / 'build/Tools/WITCHMVRKPluginRender'
    vst = witch / 'build/VST3/WITCHMVRK.vst3'
    ir_renderer = ROOT / 'build/IRComposerIntegrationTests'
    di = witch / 'measurements/di - 25 Earth Elemental - rythm gtr 2.wav'
    calibrated = witch / 'measurements/ML IR Color Calibrated.wav'
    development = witch / 'measurements/IR.wav'
    with sf.SoundFile(di) as f:
        f.seek(6 * f.samplerate)
        x = f.read(4 * f.samplerate, dtype='float32')
        source_rate = f.samplerate
    divisor = np.gcd(RATE, source_rate)
    x = resample_poly(x, RATE // divisor, source_rate // divisor)
    # Include attack and silence after the riff to expose an unintended repeated tail.
    x = np.concatenate([np.zeros(RATE // 10), x, np.zeros(RATE)]).astype(np.float32)
    input_path = out / 'di.wav'
    sf.write(input_path, x, RATE, subtype='FLOAT')
    h, hr = sf.read(calibrated, dtype='float32', always_2d=True)
    assert hr == RATE
    shifted = out / 'calibrated-with-150ms-leading-silence.wav'
    sf.write(shifted, np.concatenate([np.zeros((7200, h.shape[1])), h]), RATE, subtype='FLOAT')
    configurations = {'one_ir': (calibrated, '-'),
                      'two_aligned_copies': (calibrated, shifted),
                      'two_different_irs': (calibrated, development)}
    report = {'sample_rate': RATE, 'input_seconds': len(x) / RATE,
              'cabinet_enabled': False, 'pedals_bypass': True, 'post_fx_bypass': True,
              'ir_combine_seconds': 0.25, 'auto_align': True, 'master_eq': 'inactive',
              'chain': 'WITCHMVRK VST3 -> lossless float WAV -> production IR Composer processor',
              'reference': 'scipy.signal.fftconvolve(amp_output, exported_mix_kernel), clipped at +/-4 like the processor',
              'sha256': {str(p): sha(p) for p in [renderer, ir_renderer, di, calibrated, development,
                            vst / 'Contents/MacOS/WITCHMVRK']},
              'cases': [], 'passed': True}
    comparisons = {}
    for head in ('hybrid', 'sentinel', 'wraith'):
        for gain, controls in [('normal', (55, 50, 58)), ('high', (90, 90, 100))]:
            for block in (128, 512):
                tag = f'{head}-{gain}-b{block}'
                amp_path = out / f'{tag}-amp.wav'
                log = run([renderer, vst, input_path, amp_path, head, block, *controls, 1, 1, 100],
                          out / f'{tag}-amp.log')
                assert 'parameter Developer Cabinet = Off' in log
                assert 'parameter Pedals Bypass = On' in log and 'parameter Post FX Bypass = On' in log
                amp, sr = sf.read(amp_path, dtype='float64', always_2d=True)
                assert sr == RATE and np.isfinite(amp).all() and np.max(np.abs(amp)) > 1e-5
                one = None
                for name, irs in configurations.items():
                    stem = f'{tag}-{name}'
                    wave, kernel_path = out / f'{stem}.wav', out / f'{stem}-kernel.wav'
                    run([ir_renderer, '--chain-render', amp_path, wave, kernel_path, *irs, block],
                        out / f'{stem}.log')
                    y, _ = sf.read(wave, dtype='float64', always_2d=True)
                    kernel, _ = sf.read(kernel_path, dtype='float64', always_2d=True)
                    expected = np.column_stack([fftconvolve(amp[:, c], kernel[:, c])[:len(amp)] for c in range(2)])
                    expected = np.clip(expected, -4.0, 4.0)
                    error = y - expected
                    peak_error = float(np.max(np.abs(error)))
                    snr = float(20 * np.log10(max(np.linalg.norm(expected), 1e-30) / max(np.linalg.norm(error), 1e-30)))
                    row = {'head': head, 'gain': gain, 'block': block, 'irs': name,
                           'peak': float(np.max(np.abs(y))), 'rms': float(np.sqrt(np.mean(y*y))),
                           'reference_peak_error': peak_error, 'reference_snr_db': snr,
                           'safety_ceiling_samples': int(np.sum(np.abs(y) >= 3.99999)),
                           'finite': bool(np.isfinite(y).all()),
                           'passed': bool(np.isfinite(y).all() and snr > 90 and peak_error < 2e-4)}
                    if name == 'one_ir':
                        one = y
                    elif name == 'two_aligned_copies':
                        row['difference_from_one_ir_peak'] = float(np.max(np.abs(y - one)))
                        row['passed'] &= row['difference_from_one_ir_peak'] < 2e-4
                    if block == 128:
                        comparisons[(head, gain, name)] = y
                    else:
                        row['difference_between_block_sizes_peak'] = float(np.max(np.abs(y - comparisons[(head, gain, name)])))
                    report['cases'].append(row)
                    report['passed'] &= row['passed']
                    (out / 'metrics.json').write_text(json.dumps(report, indent=2))
                    print(f'{stem}: {"PASS" if row["passed"] else "FAIL"}, reference SNR={snr:.1f} dB, peak error={peak_error:.3g}', flush=True)
                    # Attenuation only for convenient listening; raw measurements remain untouched.
                    scale = min(1.0, 10**(-1/20) / max(row['peak'], 1e-20))
                    sf.write(out / f'{stem}-listen.wav', y * scale, RATE, subtype='PCM_24')
    print(f'Completed {len(report["cases"])} cases; passed={report["passed"]}', flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
