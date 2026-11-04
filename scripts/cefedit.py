"""Exact-text edits that keep a file's own line endings (CEF sources here are CRLF).
Usage from other scripts: from cefedit import edit; edit(path, [(old, new), ...]) with LF text."""
def edit(path, pairs):
    raw = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in raw
    text = raw.replace('\r\n', '\n')
    for old, new in pairs:
        assert text.count(old) == 1, (path, old[:80], text.count(old))
        text = text.replace(old, new, 1)
    if crlf:
        text = text.replace('\n', '\r\n')
    open(path, 'w', encoding='utf-8', newline='').write(text)
