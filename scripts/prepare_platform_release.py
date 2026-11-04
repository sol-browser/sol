"""Promote the verified platform-codec bodies into CEF's native patch stack.

Local preparation only: preserves exact before/after bytes and exports a -p0
Chromium patch. Git pinning/commit and the official build are separate steps.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
from platform_codecs_candidate import EDITS, transform

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('E:/cef-build/code/chromium/src'))
    p.add_argument('--receipt',type=Path,required=True)
    a=p.parse_args();source=a.source.resolve();receipt=a.receipt.resolve()
    if receipt.exists():raise ValueError('Use a fresh receipt directory')
    edits={k:list(v) for k,v in EDITS.items() if not k.startswith('out/')}
    edits['media/filters/win/media_foundation_audio_decoder.cc']=edits['media/filters/win/media_foundation_audio_decoder.cc'][:1]
    old,new=edits['media/filters/win/media_foundation_audio_decoder.cc'][0]
    new=new.replace('      if (config.profile() !=', '      if (BUILDFLAG(RED_PLATFORM_CODECS) && config.profile() !=')
    edits['media/filters/win/media_foundation_audio_decoder.cc']=[(old,new)]
    edits['media/BUILD.gn'].append(('    "USE_PROPRIETARY_CODECS=$proprietary_codecs",','    "USE_PROPRIETARY_CODECS=$proprietary_codecs",\n    "RED_PLATFORM_CODECS=$red_platform_codecs",'))
    old,new=edits['media/formats/mp4/mp4_stream_parser.cc'][0]
    new=new.replace('#if !BUILDFLAG(IS_WIN)','#if !BUILDFLAG(RED_PLATFORM_CODECS)')
    edits['media/formats/mp4/mp4_stream_parser.cc']=[(old,new)]
    old,new=edits['media/mojo/services/gpu_mojo_media_client_win.cc'][0]
    new=new.replace('    if (InitializeMediaFoundation()) {','    if (BUILDFLAG(RED_PLATFORM_CODECS) && InitializeMediaFoundation()) {',1)
    edits['media/mojo/services/gpu_mojo_media_client_win.cc']=[(old,new)]
    # These files already use media buildflags transitively. Include the
    # generated contract explicitly now that they reference this new flag.
    for name in ('media/formats/mp4/mp4_stream_parser.cc','media/filters/win/media_foundation_audio_decoder.cc','media/mojo/services/gpu_mojo_media_client_win.cc'):
        text=(source/name).read_text(encoding='utf-8')
        if '#include "media/media_buildflags.h"' not in text:
            anchor=next(line for line in text.splitlines() if line.startswith('#include "'))
            edits[name].append((anchor,anchor+'\n#include "media/media_buildflags.h"'))
    prepared=[];patch=[]
    for name,pairs in edits.items():
        before=(source/name).read_bytes();after=transform(before,pairs)
        prepared.append((name,before,after))
        patch.extend(difflib.unified_diff(before.decode().replace('\r\n','\n').splitlines(True),after.decode().replace('\r\n','\n').splitlines(True),fromfile=name,tofile=name))
    target=source/'cef/patch/patches/red_15_platform_codecs.patch'
    if target.exists():raise ValueError('Platform patch already exists')
    receipt.mkdir(parents=True)
    files=[]
    for i,(name,before,after) in enumerate(prepared):
        (receipt/f'{i}.before').write_bytes(before);(receipt/f'{i}.after').write_bytes(after)
        files.append({'path':name,'before':hashlib.sha256(before).hexdigest(),'after':hashlib.sha256(after).hexdigest()})
    (receipt/'manifest.json').write_text(json.dumps({'source':str(source),'files':files},indent=2)+'\n')
    patch_text=''.join(patch)
    target.write_text(patch_text,encoding='utf-8',newline='\n')
    (receipt/'platform-codecs.patch').write_text(patch_text,encoding='utf-8',newline='\n')
    cfg=source/'cef/patch/patch.cfg';raw=cfg.read_bytes()
    (receipt/'patch.cfg.before').write_bytes(raw)
    updated=transform(raw,[("    'name': 'red_13_page_container_background',\n  },", "    'name': 'red_13_page_container_background',\n  },\n  {\n    # Windows x64: D3D11 H.264 / Media Foundation AAC, no FFmpeg decoders.\n    'name': 'red_15_platform_codecs',\n  },")])
    for name,_,after in prepared:(source/name).write_bytes(after)
    cfg.write_bytes(updated)
    print(json.dumps({'receipt':str(receipt),'patch':str(target),'files':len(prepared)}))

if __name__=='__main__':main()
