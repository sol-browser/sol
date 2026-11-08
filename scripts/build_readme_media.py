"""Frame real README captures and assemble a short, explicitly edited tour.

Requires Pillow. Run readme_demo.py --capture first. Application pixels are
only scaled and framed; no controls, tabs or feature states are painted over.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT/'artifacts/readme-demo/captures'
OUTPUT = ROOT/'assets/readme'
TITLES = {
    'overview': ('01 / SPACES', 'A place for every part of your day.'),
    'split-view': ('02 / SPLIT VIEW', 'Keep both in view.'),
    'link-peek': ('03 / LINK PEEK', 'Follow your curiosity. Keep your place.'),
    'themes': ('04 / THEMES', 'Make yourself at home.'),
    'dark-space': ('05 / YOUR OWN MOOD', 'A different space. A different mood.'),
}


def latest(scene):
    candidates = [p for p in CAPTURES.glob(scene+'-*') if (p/'capture.json').exists()]
    if not candidates:
        raise SystemExit(f'Missing capture: {scene}')
    path = max(candidates,key=lambda p:(p/'capture.json').stat().st_mtime_ns)
    report = json.loads((path/'capture.json').read_text())
    if scene=='space-setup':
        if not report.get('passed'):
            raise SystemExit(f'Space editor capture failed: {path}')
        return path,report
    if not report.get('capture_only') and not (report['shutdown'] and report['exit_code']==0 and
            report['probe_results'] and all(r['pass'] for r in report['probe_results'])):
        raise SystemExit(f'Native capture failed: {path}')
    return path, report


def background(size, dark=False):
    image = Image.new('RGB',size)
    draw = ImageDraw.Draw(image)
    for y in range(size[1]):
        t=y/(size[1]-1)
        colors=((31,42,38),(34,29,45)) if dark else ((215,227,217),(239,234,222))
        draw.line((0,y,size[0],y),fill=tuple(round(a+(b-a)*t) for a,b in zip(*colors)))
    return image


def framed(source, size=(1600,1100), title=None, dark=False):
    image=background(size,dark).convert('RGBA')
    padding=32 if title else 64
    top=88 if title else padding
    bottom=40 if title else padding
    content=ImageOps.contain(source.convert('RGB'),(size[0]-padding*2,size[1]-top-bottom),Image.Resampling.LANCZOS)
    x=(size[0]-content.width)//2
    y=top+(size[1]-top-bottom-content.height)//2
    mask=Image.new('L',size)
    ImageDraw.Draw(mask).rounded_rectangle((x,y+10,x+content.width,y+content.height+10),radius=12,fill=70)
    shadow=Image.new('RGBA',size,(20,39,29,0))
    shadow.putalpha(mask.filter(ImageFilter.GaussianBlur(18)))
    image=Image.alpha_composite(image,shadow)
    rounding=Image.new('L',content.size)
    ImageDraw.Draw(rounding).rounded_rectangle((0,0,content.width-1,content.height-1),radius=9,fill=255)
    image.paste(content,(x,y),rounding)
    if title:
        draw=ImageDraw.Draw(image)
        font_root=Path('C:/Windows/Fonts')
        small=ImageFont.truetype(str(font_root/'segoeui.ttf'),11)
        large=ImageFont.truetype(str(font_root/'segoeuib.ttf'),22)
        draw.text((32,16),title[0],font=small,fill='#52695a')
        draw.text((32,34),title[1],font=large,fill='#243c2e')
        draw.text((32,size[1]-23),'SOL',font=small,fill='#52695a')
    return image.convert('RGB')


def build_space():
    path,report=latest('space-setup')
    # Omit the wait for foreground activation; keep the actual editor sequence.
    frames=report['frames']
    if len(frames)<12:
        raise SystemExit('Space editor recording is too short')
    start=next((index for index in range(1,len(frames)) if frames[index]['seconds']-frames[index-1]['seconds']>.5),1)
    clip=frames[start:]
    title=('YOUR SPACE', 'Name it. Color it. Make it yours.')
    images=[framed(Image.open(path/frames[0]['file']),(1120,860),title)]
    durations=[900]
    for index,frame in enumerate(clip):
        images.append(framed(Image.open(path/frame['file']),(1120,860),title))
        durations.append(max(60,round((clip[index+1]['seconds']-frame['seconds'])*1000)) if index+1<len(clip) else 1500)
    swatches=Image.new('RGB',(280,215*len(images)))
    for index,image in enumerate(images):
        swatches.paste(image.resize((280,215)),(0,index*215))
    palette=swatches.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    quantized=[image.quantize(palette=palette,dither=Image.Dither.NONE) for image in images]
    target=OUTPUT/'space-setup.gif'
    quantized[0].save(target,save_all=True,append_images=quantized[1:],duration=durations,loop=0,optimize=True,disposal=1)
    with Image.open(target) as gif:
        duration=0
        for index in range(gif.n_frames):
            gif.seek(index)
            duration+=gif.info.get('duration',0)
        encoded_frames=gif.n_frames
    result=dict(source=str(path.relative_to(ROOT)),frames=encoded_frames,duration_ms=duration,bytes=target.stat().st_size,
                sha256=hashlib.sha256(target.read_bytes()).hexdigest(),checks=report['checks'])
    (CAPTURES/'space-media-receipt.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--space-only',action='store_true',help='Build only the separately recorded native Space configuration GIF.')
    args=parser.parse_args()
    OUTPUT.mkdir(parents=True,exist_ok=True)
    if args.space_only:
        build_space()
        return
    scenes={name:latest(name) for name in TITLES}
    provenance={}
    for name,(path,report) in scenes.items():
        provenance[name]=dict(source=str(path.relative_to(ROOT)).replace('\\','/'),native=report)
        if name!='themes':
            framed(Image.open(path/'screen.png')).save(OUTPUT/f'{name}.webp',quality=94,method=6)
    frames=[]
    durations=[]
    def add(image,name,duration):
        frames.append(framed(image,(960,720),TITLES[name]))
        durations.append(duration)
    for name in ('overview','split-view'):
        add(Image.open(scenes[name][0]/'screen.png'),name,1800)
    peek_path,peek_report=scenes['link-peek']
    clip=[f for f in peek_report['frames'] if 1.05<=f['seconds']<=1.4]
    if not clip:
        raise SystemExit('Recapture link-peek to include its actual opening frames.')
    # PrintWindow cannot reliably compose the native child-window transform
    # during Peek entry. Use an explicit before/after cut between settled
    # captures, rather than publishing incomplete white capture surfaces.
    add(Image.open(peek_path/clip[0]['file']),'link-peek',800)
    add(Image.open(peek_path/'screen.png'),'link-peek',1800)
    theme_path,theme_report=scenes['themes']
    clip=[f for f in theme_report['frames'] if f['seconds']<7.6]
    for index,frame in enumerate(clip):
        duration=round((clip[index+1]['seconds']-frame['seconds'])*1000) if index+1<len(clip) else 1000
        add(Image.open(theme_path/frame['file']),'themes',duration)
    add(Image.open(scenes['dark-space'][0]/'screen.png'),'dark-space',1800)
    # One shared palette avoids color flicker between GIF frames.
    swatches=Image.new('RGB',(320,220*len(frames)))
    for index,frame in enumerate(frames):
        swatches.paste(frame.resize((320,220)),(0,index*220))
    palette=swatches.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    quantized=[frame.quantize(palette=palette,dither=Image.Dither.NONE) for frame in frames]
    quantized[0].save(OUTPUT/'demo.gif',save_all=True,append_images=quantized[1:],duration=durations,loop=0,optimize=True,disposal=1)
    # Keep detailed local provenance out of the public media directory.
    receipt=dict(captures=provenance,frames=len(frames),duration_ms=sum(durations),assets={
        p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for p in OUTPUT.iterdir() if p.suffix in ('.webp','.gif')})
    (CAPTURES/'media-receipt.json').write_text(json.dumps(receipt,indent=2))
    print(json.dumps({k:v for k,v in receipt.items() if k!='captures'},indent=2))


if __name__=='__main__':
    main()
