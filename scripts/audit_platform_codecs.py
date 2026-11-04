"""Audit the exact staged FFmpeg DLL and its generated build graph."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def audit(runtime, source):
    dll_path = (runtime / 'ffmpeg.dll').resolve()
    # Resolve only the staged runtime's dependencies; never search arbitrary cwd.
    with os.add_dll_directory(str(runtime.resolve())):
        dll = ctypes.CDLL(str(dll_path))
    find_decoder = dll.avcodec_find_decoder
    find_decoder.argtypes = [ctypes.c_int]
    find_decoder.restype = ctypes.c_void_p
    # FFmpeg AVCodecID ordinals from libavcodec/codec_id.h (pinned source).
    decoders = {name: bool(find_decoder(value)) for name, value in
                [('h264', 27), ('aac', 0x15002), ('mp3', 0x15001)]}
    config = (source / 'third_party/ffmpeg/chromium/config/Chromium/win/x64/config_components.h').read_text()
    generated = source / 'out/Dev_x64/obj/third_party/ffmpeg/ffmpeg_internal.ninja'
    graph = generated.read_text()
    compiled_sources = re.findall(r'^build .*?: cc (\S+)', graph, re.M)
    forbidden = [p for p in compiled_sources if re.search(r'/(?:h264dec|h264_cabac|h264_cavlc|aacdec(?:_\w+)?)\.c$', p)
                 or '/libavcodec/aac/' in p]
    checks = {
        'no_registered_h264': decoders['h264'] is False,
        'no_registered_aac': decoders['aac'] is False,
        'mp3_control_available': decoders['mp3'] is True,
        'generated_graph_present': len(compiled_sources) > 50,
        'no_decoder_implementation_in_graph': not forbidden,
        'config_decoders_disabled': all(re.search(r'^#define CONFIG_' + codec + r'_DECODER 0$', config, re.M)
                                        for codec in ('H264', 'AAC', 'AAC_LATM')),
        'mov_demuxer_retained': '#define CONFIG_MOV_DEMUXER 1' in config,
    }
    return {'passed': all(checks.values()), 'checks': checks, 'registeredDecoders': decoders,
            'runtimeSha256': {name: hashlib.sha256((runtime / name).read_bytes()).hexdigest()
                              for name in ('ffmpeg.dll', 'media.dll', 'media_mojo_services.dll',
                                           'libcef.dll', 'sol.exe', 'sol.dll')},
            'dll': str(dll_path), 'sha256': hashlib.sha256(dll_path.read_bytes()).hexdigest(),
            'graph': str(generated), 'graphSha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
            'compiledSources': len(compiled_sources), 'forbiddenSources': forbidden}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / 'build/devplatform/apps/desktop/Release')
    parser.add_argument('--source', type=Path, default=Path('E:/cef-build/code/chromium/src'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.runtime, args.source)
    args.output.with_suffix('.ninja').write_bytes(Path(result['graph']).read_bytes())
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['passed']:
        raise SystemExit(1)
