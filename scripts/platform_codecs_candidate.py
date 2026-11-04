"""Transactional, local-only Windows platform codec experiment for the pinned engine.

Never updates source/binary locks. Apply snapshots the exact originals and refuses
unexpected source; restore refuses to overwrite edits made after our experiment.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EDITS = {
    'media/media_options.gni': [(
        '  media_use_ffmpeg = true',
        '  media_use_ffmpeg = true\n\n'
        '  # Red Windows experiment: OS AAC and hardware-only H.264.\n'
        '  red_platform_codecs = false')],
    'media/BUILD.gn': [(
        '      ffmpeg_branding != "Chromium",',
        '      ffmpeg_branding != "Chromium" || red_platform_codecs,'), (
        '# Common configuration for targets in the media directory;',
        'assert(!red_platform_codecs ||\n'
        '       (is_win && target_cpu == "x64" && proprietary_codecs &&\n'
        '        ffmpeg_branding == "Chromium"),\n'
        '       "Red platform codecs require Windows x64 and Chromium FFmpeg")\n\n'
        '# Common configuration for targets in the media directory;')],
    'media/filters/win/media_foundation_audio_decoder.cc': [(
        '      if (config.profile() == AudioCodecProfile::kXHE_AAC &&\n'
        '          base::win::GetVersion() >= base::win::Version::WIN11_22H2) {',
        '      // Ordinary AAC uses the existing RAW AAC + ASC path.\n'
        '      if (config.profile() != AudioCodecProfile::kXHE_AAC) {\n'
        '        return MFT_REGISTER_TYPE_INFO{MFMediaType_Audio, MFAudioFormat_AAC};\n'
        '      }\n'
        '      if (config.profile() == AudioCodecProfile::kXHE_AAC &&\n'
        '          base::win::GetVersion() >= base::win::Version::WIN11_22H2) {'), (
        '#include <stdint.h>', '#include <stdint.h>\n#include <atomic>\n#include <cmath>\n#include <cstring>'), (
        '  // Output is always configured to be interleaved float.',
        '  // Local experiment diagnostic: prove the OS returned non-silent PCM.\n'
        '  static std::atomic<bool> logged_pcm{false};\n'
        '  if (config_.codec() == AudioCodec::kAAC) {\n'
        '    float peak = 0;\n'
        '    for (size_t i = 0; i + sizeof(float) <= destination.size(); i += sizeof(float)) {\n'
        '      float value;\n'
        '      std::memcpy(&value, destination.subspan(i, sizeof(float)).data(), sizeof(float));\n'
        '      peak = std::max(peak, std::abs(value));\n'
        '    }\n'
        '    static std::atomic<int> pcm_buffers{0};\n'
        '    if (pcm_buffers.fetch_add(1) < 3 || (peak > 0.00001f && !logged_pcm.exchange(true))) {\n'
        '      LOG(WARNING) << "RED_PLATFORM_AAC_PCM peak=" << peak;\n'
        '    }\n'
        '  }\n'
        '  // Output is always configured to be interleaved float.')],
    'media/ffmpeg/ffmpeg_common.cc': [(
        '  ChannelLayout channel_layout =\n'
        '      ChannelLayoutToChromeChannelLayout(codec_context->ch_layout);',
        '  // Without a bundled AAC decoder FFmpeg cannot infer a decoded sample\n'
        '  // format. As in the MP4 stream parser, use S16 as the nominal format\n'
        '  // of compressed AAC; the platform decoder negotiates actual PCM.\n'
        '  if (codec == AudioCodec::kAAC && sample_format == kUnknownSampleFormat) {\n'
        '    sample_format = kSampleFormatS16;\n'
        '  }\n\n'
        '  ChannelLayout channel_layout =\n'
        '      ChannelLayoutToChromeChannelLayout(codec_context->ch_layout);'), (
        '  DCHECK_EQ(av_get_bytes_per_sample(codec_context->sample_fmt),\n'
        '            config->bytes_per_channel());',
        '  if (codec != AudioCodec::kAAC || codec_context->sample_fmt != AV_SAMPLE_FMT_NONE) {\n'
        '    DCHECK_EQ(av_get_bytes_per_sample(codec_context->sample_fmt),\n'
        '              config->bytes_per_channel());\n'
        '  }')],
    'media/formats/mp4/mp4_stream_parser.cc': [(
        '  if (aac_config.fits_in_adts()) {\n'
        '    output_buffer = aac_config.CreateAdtsFromEsds(frame_buf, &adts_header_size);\n'
        '  } else {',
        '  // Red Windows platform-only candidate: Media Foundation consumes RAW\n'
        '  // AAC + AudioSpecificConfig, just like the ordinary MP4/WebCodecs path.\n'
        '  // Preserve the ADTS preparation for other platforms.\n'
        '#if !BUILDFLAG(IS_WIN)\n'
        '  if (aac_config.fits_in_adts()) {\n'
        '    output_buffer = aac_config.CreateAdtsFromEsds(frame_buf, &adts_header_size);\n'
        '  } else\n'
        '#endif\n'
        '  {')],
    'media/mojo/services/gpu_mojo_media_client_win.cc': [(
        '    if (base::win::GetVersion() >= base::win::Version::WIN11_22H2 &&\n'
        '        InitializeMediaFoundation()) {\n'
        '      audio_configs.emplace_back(AudioCodec::kAAC, AudioCodecProfile::kXHE_AAC);\n'
        '    }',
        '    if (InitializeMediaFoundation()) {\n'
        '      // AAC-LC / HE-AAC have kUnknown profile in Chromium.\n'
        '      audio_configs.emplace_back(AudioCodec::kAAC, AudioCodecProfile::kUnknown);\n'
        '    }\n'
        '    if (base::win::GetVersion() >= base::win::Version::WIN11_22H2 &&\n'
        '        InitializeMediaFoundation()) {\n'
        '      audio_configs.emplace_back(AudioCodec::kAAC, AudioCodecProfile::kXHE_AAC);\n'
        '    }')],
    'out/Dev_x64/args.gn': [('ffmpeg_branding="Chrome"',
                           'ffmpeg_branding="Chromium"\nred_platform_codecs=true')],
}


def transform(raw, pairs):
    text = raw.decode('utf-8')
    crlf = '\r\n' in text
    text = text.replace('\r\n', '\n')
    for old, new in pairs:
        if text.count(old) != 1:
            raise ValueError('Unexpected source, refusing edit: ' + old[:80])
        text = text.replace(old, new, 1)
    return (text.replace('\n', '\r\n') if crlf else text).encode('utf-8')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def apply(source, receipt):
    if receipt.exists():
        raise ValueError('Receipt already exists; use a fresh experiment directory')
    lock = json.loads((ROOT / 'cmake/cef-source.lock.json').read_text())
    for path, expected in [(source, lock['chromium_revision']), (source / 'cef', lock['revision'])]:
        actual = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise ValueError(f'Engine identity mismatch: {path}: {actual}')
    prepared = []
    for relative, pairs in EDITS.items():
        path = source / relative
        before = path.read_bytes()
        after = transform(before, pairs)
        prepared.append((relative, before, after))
    receipt.mkdir(parents=True)
    manifest = {'source': str(source), 'pins': lock, 'files': []}
    patch = []
    for i, (relative, before, after) in enumerate(prepared):
        (receipt / f'{i}.before').write_bytes(before)
        (receipt / f'{i}.after').write_bytes(after)
        manifest['files'].append({'path': relative, 'before': sha(before), 'after': sha(after)})
        patch.extend(difflib.unified_diff(before.decode().replace('\r\n', '\n').splitlines(True),
                                         after.decode().replace('\r\n', '\n').splitlines(True),
                                         fromfile='a/' + relative, tofile='b/' + relative))
    (receipt / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (receipt / 'candidate.patch').write_text(''.join(patch), newline='\n')
    try:
        for relative, before, after in prepared:
            (source / relative).write_bytes(after)
    except Exception:
        restore(receipt)
        raise
    print(json.dumps({'applied': str(receipt), 'files': len(prepared)}))


def restore(receipt):
    manifest = json.loads((receipt / 'manifest.json').read_text())
    source = Path(manifest['source'])
    originals = []
    for i, item in enumerate(manifest['files']):
        path = source / item['path']
        before = (receipt / f'{i}.before').read_bytes()
        if sha(before) != item['before'] or sha(path.read_bytes()) not in (item['before'], item['after']):
            raise ValueError('Concurrent change or corrupted snapshot: ' + str(path))
        originals.append((path, before))
    for path, before in originals:
        path.write_bytes(before)
    # Export with normalized LF even when the captured upstream files use CRLF.
    patch = []
    for i, item in enumerate(manifest['files']):
        before = (receipt / f'{i}.before').read_text().splitlines(True)
        after = (receipt / f'{i}.after').read_text().splitlines(True)
        patch.extend(difflib.unified_diff(before, after, fromfile='a/' + item['path'], tofile='b/' + item['path']))
    (receipt / 'candidate.patch').write_text(''.join(patch), newline='\n')
    print(json.dumps({'restored': str(receipt), 'files': len(originals)}))


def verify_restored(receipt):
    manifest = json.loads((receipt / 'manifest.json').read_text())
    checks = {item['path']: sha((Path(manifest['source']) / item['path']).read_bytes()) == item['before']
              for item in manifest['files']}
    result = {'restoredExactBytes': all(checks.values()), 'files': checks}
    (receipt / 'restoration.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['restoredExactBytes']:
        raise ValueError('Engine sources differ from captured originals')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('apply', 'restore', 'verify'))
    parser.add_argument('--source', type=Path, default=Path('E:/cef-build/code/chromium/src'))
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'apply':
        apply(args.source.resolve(), args.receipt.resolve())
    elif args.mode == 'restore':
        restore(args.receipt.resolve())
    else:
        verify_restored(args.receipt.resolve())
