"""Build code-native README icons, download artwork and a real dark cover."""
import hashlib
import json
from PIL import Image
from build_readme_media import ROOT, OUTPUT, CAPTURES, framed, latest

ICONS = {
    'spaces': '<rect x="4" y="4" width="19" height="21" rx="4"/><rect x="10" y="10" width="18" height="18" rx="4" fill="#dfe8d8"/>',
    'split': '<rect x="3" y="6" width="26" height="21" rx="4"/><path d="M16 6v21M8 12h3m10 0h3"/>',
    'peek': '<rect x="3" y="4" width="20" height="22" rx="4"/><rect x="10" y="11" width="19" height="17" rx="4" fill="#dfe8d8"/><path d="M15 16h9m-9 5h6"/>',
    'profiles': '<circle cx="12" cy="11" r="4"/><path d="M4 27v-3a8 8 0 0 1 16 0v3m1-19a4 4 0 0 1 0 8m4 11v-3a8 8 0 0 0-3-6"/>',
    'palette': '<circle cx="10" cy="12" r="6"/><circle cx="22" cy="12" r="6"/><circle cx="16" cy="23" r="6"/>',
    'import': '<path d="M16 3v17m-6-6 6 6 6-6M5 21v5a3 3 0 0 0 3 3h16a3 3 0 0 0 3-3v-5"/>',
}


def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    for name,paths in ICONS.items():
        (OUTPUT/f'feature-{name}.svg').write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32" fill="none" stroke="#739080" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'+paths+'</svg>\n',encoding='utf-8')
    (OUTPUT/'download.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="350" height="60" viewBox="0 0 350 60">
  <rect width="350" height="60" rx="14" fill="#314f42"/>
  <g fill="none" stroke="#f8f6ef" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">
    <path d="M27 18v17m-6-6 6 6 6-6M19 40h16"/>
    <path d="m321 35 10-10m-10 0h10v10"/>
  </g>
  <text x="52" y="35.5" fill="#f8f6ef" font-family="Segoe UI,Arial,sans-serif" font-size="16" font-weight="600">Download Sol for Windows</text>
</svg>\n''',encoding='utf-8')
    source,_=latest('dark-space')
    framed(Image.open(source/'screen.png'),dark=True).save(OUTPUT/'cover-dark.webp',quality=94,method=6)
    names=['cover-dark.webp','download.svg']+[f'feature-{name}.svg' for name in ICONS]
    receipt=dict(dark_cover_source=str(source.relative_to(ROOT)).replace('\\','/'),assets={
        name:dict(bytes=(OUTPUT/name).stat().st_size,sha256=hashlib.sha256((OUTPUT/name).read_bytes()).hexdigest()) for name in names})
    (CAPTURES/'branding-receipt.json').write_text(json.dumps(receipt,indent=2))
    print('Built dark cover, download artwork and six feature icons.')


if __name__=='__main__':
    main()
