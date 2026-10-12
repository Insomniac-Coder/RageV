"""Every tool call, in every session transcript (subagents included), that could have written a file inside
one of the RageV copies: Edit/Write/NotebookEdit with a path there, and shell commands naming a copy."""
import glob, json, os, re

COPY = re.compile(r'(RageV-before|RageV-bisect|RageV-head|ragev-pre-restir)', re.I)
ROOT = 'C:/Users/ism19/.claude/projects'
files = glob.glob(ROOT + '/**/*.jsonl', recursive=True)
hits = []
for f in files:
    for line in open(f, encoding='utf-8', errors='replace'):
        if not COPY.search(line):
            continue
        try:
            rec = json.loads(line)
        except ValueError:
            continue
        msg = rec.get('message') or {}
        content = msg.get('content') if isinstance(msg, dict) else None
        if not isinstance(content, list):
            continue
        for c in content:
            if c.get('type') != 'tool_use':
                continue
            name, inp = c.get('name'), c.get('input') or {}
            if name in ('Edit', 'Write', 'NotebookEdit', 'MultiEdit'):
                path = inp.get('file_path') or inp.get('notebook_path') or ''
                if COPY.search(path):
                    hits.append((rec.get('timestamp', '')[:16], os.path.basename(f)[:8], name, path))
            elif name in ('Bash', 'PowerShell'):
                cmd = inp.get('command', '')
                if COPY.search(cmd):
                    hits.append((rec.get('timestamp', '')[:16], os.path.basename(f)[:8], name,
                                 cmd.replace('\n', ' ')[:400]))
print('%d transcripts scanned, %d calls name a copy' % (len(files), len(hits)))
for h in sorted(hits):
    print(' | '.join(h))
