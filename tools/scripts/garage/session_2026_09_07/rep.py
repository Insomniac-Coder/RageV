"""Anchor-and-replace that survives this tree's mixed line endings.

Renderer3D.cpp and PostProcess.cpp are CRLF throughout; pbr_fragment.glsl is
mixed in places. A patch written with LF anchors silently matches nothing and
the run that follows measures an unchanged build -- the trap the handoff
records twice. Every replace here tries the text as written, then with CRLF,
and refuses to write if it matched neither or matched more than once.
"""
import io


def read(path):
    return io.open(path, encoding='utf-8', newline='').read()


def write(path, text):
    io.open(path, 'w', encoding='utf-8', newline='').write(text)


def rep(text, old, new, count=1):
    """Replace `old` with `new`, trying LF then CRLF. Asserts the count."""
    for a, b in ((old, new), (old.replace('\n', '\r\n'), new.replace('\n', '\r\n'))):
        if text.count(a) == count:
            return text.replace(a, b)
    raise AssertionError('anchor matched %d/%d (LF) and %d (CRLF): %r'
                         % (text.count(old), count,
                            text.count(old.replace('\n', '\r\n')), old[:90]))


def has(text, marker):
    return marker in text or marker.replace('\n', '\r\n') in text
