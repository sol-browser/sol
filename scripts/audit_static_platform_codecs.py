"""Audit the official monolithic engine's FFmpeg link inputs and staged identity."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def forbidden(names):
    result=[]
    for n in names:
        normal=n.replace('\\','/')
        if '/libavformat/' in normal or 'autorename_libavformat_' in normal:continue
        if re.search(r'(?:/|^)(?:autorename_libavcodec_)?(?:h264dec|h264_cabac|h264_cavlc|aacdec(?:_\w+)?)(?:\.c|\.obj)$',normal) or '/libavcodec/aac/' in normal:result.append(n)
    return result

def audit(source,out,runtime,lib_tool):
    graph_path=out/'obj/third_party/ffmpeg/ffmpeg_internal.ninja'
    graph=graph_path.read_text();args=(out/'args.gn').read_text()
    config=(source/'third_party/ffmpeg/chromium/config/Chromium/win/x64/config_components.h').read_text()
    sources=re.findall(r'^build .*?: cc (\S+)',graph,re.M)
    archive=out/'obj/third_party/ffmpeg/ffmpeg_internal.lib'
    listing=subprocess.run([str(lib_tool),'/lib','/list',str(archive)],check=True,capture_output=True,text=True).stdout
    members=[s.strip() for s in listing.splitlines() if s.strip()]
    link_inputs=re.search(r'^build obj/third_party/ffmpeg/ffmpeg_internal\.lib: alink ([^\n]+)',graph,re.M).group(1).split(' |')[0].split()
    member_names={m.replace('\\','/').rsplit('/',1)[-1] for m in members}
    input_names={m.rsplit('/',1)[-1] for m in link_inputs}
    flags=(out/'gen/media/media_buildflags.h').read_text()
    linked=(out/'obj/cef/libcef.ninja').read_text()
    checks={
        'official_monolithic':all(re.search(r'^'+key+r'\s*=\s*'+value+r'\s*$',args,re.M) for key,value in [('is_official_build','true'),('is_component_build','false'),('is_debug','false'),('proprietary_codecs','true'),('red_platform_codecs','true'),('ffmpeg_branding','"Chromium"')]),
        'platform_buildflag_enabled':bool(re.search(r'BUILDFLAG_INTERNAL_RED_PLATFORM_CODECS\(\)\s+\(1\)',flags)),
        'chromium_config_selected':'chromium/config/Chromium/win/x64' in graph and 'chromium/config/Chrome/win/x64' not in graph,
        'decoder_configs_disabled':all(re.search(r'^#define CONFIG_'+c+r'_DECODER 0$',config,re.M) for c in ('H264','AAC','AAC_LATM')),
        'mp3_control_retained':'#define CONFIG_MP3_DECODER 1' in config and any('mpegaudiodec' in m for m in members),
        'mov_demux_retained':'#define CONFIG_MOV_DEMUXER 1' in config and any('/mov.' in m.replace('\\','/') for m in members),
        'nonempty_build_inputs':len(sources)>50 and len(members)>50,
        'no_software_decoder_sources':not forbidden(sources),
        'no_software_decoder_archive_members':not forbidden(members),
        'archive_matches_generated_object_set':member_names==input_names and len(member_names)==len(members),
        'audited_archive_is_link_input':'obj/third_party/ffmpeg/ffmpeg_internal.lib' in linked,
        'matching_staged_engine':digest(runtime/'libcef.dll')==digest(out/'libcef.dll'),
        'no_component_media_dlls':not any((runtime/n).exists() for n in ('ffmpeg.dll','media.dll','media_mojo_services.dll')),
    }
    return {'passed':all(checks.values()),'checks':checks,'compiledSources':len(sources),'archiveMembers':len(members),
            'forbiddenSources':forbidden(sources),'forbiddenMembers':forbidden(members),
            'sha256':{str(p):digest(p) for p in (graph_path,archive,out/'args.gn',runtime/'libcef.dll')},
            'objectSha256':{m:digest(out/m) for m in link_inputs},'members':members}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,default=Path('E:/cef-build/code/chromium/src'))
    p.add_argument('--out',type=Path)
    p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--lib-tool',type=Path,help='Matching lld-link.exe (supports LLVM thin archives)')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=audit(a.source,a.out or a.source/'out/Release_GN_x64',a.runtime,a.lib_tool or a.source/'third_party/llvm-build/Release+Asserts/bin/lld-link.exe')
    a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k not in ('members','sha256','objectSha256')},indent=2))
    if not r['passed']:raise SystemExit('Static codec audit failed')

if __name__=='__main__':main()
