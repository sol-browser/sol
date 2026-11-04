"""Validate a completed official SDK archive, publish atomically and pin its bytes."""
import argparse
import bz2
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def digest_stream(f):
    h=hashlib.sha256()
    for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def digest(p):
    with p.open('rb') as f:return digest_stream(f)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--source',type=Path,default=Path('E:/cef-build/code/chromium/src'))
    p.add_argument('--receipt',type=Path,required=True)
    a=p.parse_args();archive=a.archive.resolve();before=archive.stat()
    lock_path=ROOT/'cmake/cef.lock.json';source_lock=json.loads((ROOT/'cmake/cef-source.lock.json').read_text())
    head=subprocess.check_output(['git','-C',str(a.source/'cef'),'rev-parse','HEAD'],text=True).strip()
    if head!=source_lock['revision']:raise ValueError('Engine source pin differs')
    root=archive.name.removesuffix('.tar.bz2')
    with bz2.open(archive,'rb') as f:
        while f.read(1024*1024):pass  # Require complete bzip2 stream and CRC.
    with tarfile.open(archive,'r:bz2') as tar:
        for m in tar.getmembers():
            parts=Path(m.name).parts
            if not parts or parts[0]!=root or '..' in parts or not(m.isfile() or m.isdir()):raise ValueError('Unsafe SDK member')
        header=tar.extractfile(root+'/include/cef_version.h').read().decode()
        version=re.search(r'#define CEF_VERSION "([^"]+)"',header).group(1)
        revision=re.search(r'#define CEF_COMMIT_HASH "([^"]+)"',header).group(1)
        browser=tar.extractfile(root+'/include/cef_browser.h').read().decode()
        if revision!=head or 'SetContentContainerCornerRadius' not in browser:raise ValueError('SDK header identity/corner API mismatch')
        with tar.extractfile(root+'/Release/libcef.dll') as f:runtime_hash=digest_stream(f)
    if runtime_hash!=digest(a.source/'out/Release_GN_x64/libcef.dll'):raise ValueError('SDK runtime differs from completed engine')
    if root!='cef_binary_'+version+'_windows64':raise ValueError('Archive name/version mismatch')
    if (archive.stat().st_size,archive.stat().st_mtime_ns)!=(before.st_size,before.st_mtime_ns):raise ValueError('SDK changed during validation')
    sha=digest(archive);cache=ROOT/'.cache';cache.mkdir(exist_ok=True)
    target=cache/(root+'-red.tar.bz2')
    fd,name=tempfile.mkstemp(prefix='.cef-publish-',suffix='.partial',dir=cache);os.close(fd);temporary=Path(name)
    try:
        shutil.copyfile(archive,temporary)
        if digest(temporary)!=sha:raise ValueError('SDK changed while copying')
        if target.exists():
            if digest(target)!=sha:raise ValueError('Refusing to replace different cached SDK bytes')
        else:
            try:os.rename(temporary,target)  # Windows rename fails if another publisher won.
            except FileExistsError:
                if digest(target)!=sha:raise
    finally:
        if temporary.exists():temporary.unlink()
    old=json.loads(lock_path.read_text());new={**old,'cef_version':version,'cef_revision':head,
        'root':root,'archive':target.name,'size':before.st_size,'sha256':sha,
        'distribution':'standard (local sol-156 official Release; Windows platform codecs)',
        'gn_defines':'is_official_build=true proprietary_codecs=true ffmpeg_branding=Chromium red_platform_codecs=true',
        'locked_on':'2026-10-29'}
    lock_path.write_text(json.dumps(new,indent=2)+'\n')
    receipt={'validated':True,'cefVersion':version,'revision':head,'archive':str(target),'size':before.st_size,
             'sha256':sha,'runtimeSha256':runtime_hash,'previousArchive':old['archive'],'completeBzip2':True}
    a.receipt.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
