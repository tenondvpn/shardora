"""
Build shardora_whitepaper_v2.pdf
Pipeline: Markdown → pandoc (no standalone) → wrap HTML → inject CSS + base64 → Chrome PDF
"""
import subprocess, os, sys, base64, re

DOCS  = os.path.dirname(os.path.abspath(__file__))
MD    = os.path.join(DOCS, 'shardora_whitepaper_v2.md')
HTML  = os.path.join(DOCS, '_wp_v2_tmp.html')
PDF   = os.path.join(DOCS, 'shardora_whitepaper_v2.pdf')
CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'

CSS = r"""
@font-face { font-family:'CJK'; src:local('Microsoft YaHei'),local('SimHei'); }
@page {
  size: A4;
  margin: 22mm 20mm 24mm 20mm;
  @bottom-center {
    content: counter(page) " / " counter(pages);
    font-size:9pt; color:#888; font-family:'CJK',sans-serif;
  }
}
* { box-sizing:border-box; }
body {
  font-family:'CJK','Microsoft YaHei','SimHei',sans-serif;
  font-size:11pt; line-height:1.85; color:#111827; background:#fff;
  max-width:100%;
}
/* Cover title block */
.cover-title {
  text-align:center; margin:40mm 0 30mm 0;
}
.cover-title h1 {
  font-size:28pt; font-weight:800; color:#0f3460;
  border:none; margin-bottom:8px; padding-bottom:0;
}
.cover-title .subtitle {
  font-size:14pt; color:#555; margin-bottom:6px;
}
.cover-title .version {
  font-size:11pt; color:#888;
}
/* No top-level H1 in body (cover is separate) */
h1 { display:none; }
h2 {
  font-size:15pt; color:#0f3460; margin-top:1.6em; margin-bottom:0.4em;
  border-bottom:2.5px solid #e94560; padding-bottom:4px;
  page-break-after:avoid;
}
h3 {
  font-size:12.5pt; color:#16213e; margin-top:1.2em; margin-bottom:0.3em;
  page-break-after:avoid;
}
h4 {
  font-size:11.5pt; color:#0f3460; margin-top:1em; margin-bottom:0.2em;
}
p { margin:.5em 0; text-align:justify; }
img {
  display:block; max-width:100%; width:100%; margin:1.2em auto;
  border-radius:5px; border:1px solid #dde; page-break-inside:avoid;
}
/* Figure captions: italic em after image paragraph */
p em:only-child {
  display:block; text-align:center;
  font-size:9pt; color:#666; margin-top:-0.8em; margin-bottom:1em;
  font-style:italic;
}
table {
  width:100%; border-collapse:collapse; font-size:9.5pt;
  margin:1em 0; page-break-inside:avoid;
}
thead { background:#0f3460; color:#fff; }
th { padding:7px 10px; text-align:left; }
td { padding:5px 10px; border-bottom:1px solid #e2e8f0; }
tr:nth-child(even) { background:#f8fafc; }
code {
  font-family:'Consolas','Courier New',monospace; font-size:9pt;
  background:#f1f5f9; padding:1px 4px; border-radius:3px; color:#c7254e;
}
blockquote {
  border-left:4px solid #e94560; margin:.8em 0; padding:6px 13px;
  background:#fff5f7; color:#555; font-style:italic;
  border-radius:0 5px 5px 0;
}
hr { border:none; border-top:1.5px solid #e2e8f0; margin:1.2em 0; }
ul, ol { margin:.5em 0 .5em 1.8em; padding:0; }
li { margin:.2em 0; }
strong { color:#0f3460; font-weight:700; }
a { color:#0f3460; text-decoration:none; }
h2, h3, h4 { page-break-after:avoid; }
.toc { background:#f8fafc; border:1px solid #e2e8f0; border-radius:6px;
       padding:16px 20px; margin:1em 0 2em 0; }
.toc p { margin:.15em 0; font-size:10.5pt; }
"""

COVER_HTML = """<div class="cover-title">
  <h1 style="display:block !important;">Shardora 白皮书</h1>
  <div class="subtitle">通过二维并行分片扩展区块链</div>
  <div class="version">版本 2.0 &nbsp;·&nbsp; 2026 年 9 月</div>
  <div class="version" style="margin-top:8px;font-size:10pt;color:#aaa;">
    学术引用：Shardora: Scaling Blockchain Sharding via 2D Parallelism<br>
    IEEE TNSE 2026. DOI: 10.1109/TNSE.2026.3684813
  </div>
</div>
<hr style="margin:0 0 2em 0;">
"""

def md_to_html_fragment():
    print("Step 1  pandoc markdown → HTML fragment")
    r = subprocess.run(
        ['pandoc', MD, '-f','markdown', '-t','html5',
         '--wrap=none', '-o', HTML],
        capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"pandoc error:\n{r.stderr}")
    print("        OK")

def build_full_html():
    print("Step 2  wrap fragment + inject CSS")
    fragment = open(HTML, encoding='utf-8').read()

    # Remove any leading H1 that pandoc may have emitted
    fragment = re.sub(r'^\s*<h1[^>]*>.*?</h1>\s*', '', fragment, flags=re.DOTALL)

    full = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<style>
{CSS}
</style>
</head>
<body>
{COVER_HTML}
{fragment}
</body>
</html>"""

    open(HTML, 'w', encoding='utf-8').write(full)
    print("        OK")

def embed_images():
    print("Step 3  embed images as base64")
    html = open(HTML, encoding='utf-8').read()

    def embed(m):
        src = m.group(1)
        if src.startswith('data:') or src.startswith('http'):
            return m.group(0)
        path = os.path.join(DOCS, src)
        if not os.path.exists(path):
            print(f"        WARNING image not found: {src}")
            return m.group(0)
        ext  = os.path.splitext(src)[1].lower().lstrip('.')
        mime = {'jpg':'image/jpeg','jpeg':'image/jpeg',
                'png':'image/png','gif':'image/gif'}.get(ext,'image/jpeg')
        b64  = base64.b64encode(open(path,'rb').read()).decode()
        kb   = os.path.getsize(path) // 1024
        print(f"        embedded {src}  ({kb} KB)")
        return f'src="data:{mime};base64,{b64}"'

    html = re.sub(r'src="([^"]+)"', embed, html)
    open(HTML, 'w', encoding='utf-8').write(html)
    print("        OK")

def chrome_to_pdf():
    print("Step 4  Chrome headless → PDF")
    file_url = 'file:///' + HTML.replace('\\', '/')
    args = [
        CHROME,
        '--headless=new',
        '--disable-gpu',
        '--no-sandbox',
        '--disable-web-security',
        '--allow-file-access-from-files',
        '--run-all-compositor-stages-before-draw',
        '--virtual-time-budget=18000',
        '--print-to-pdf-no-header',
        f'--print-to-pdf={PDF}',
        file_url,
    ]
    r = subprocess.run(args, capture_output=True, text=True, timeout=120)
    if not os.path.exists(PDF):
        sys.exit(f"Chrome failed to produce PDF.\nstderr: {r.stderr[:800]}")
    print(f"        OK  →  {PDF}  ({os.path.getsize(PDF)//1024} KB)")

def cleanup():
    if os.path.exists(HTML):
        os.remove(HTML)

if __name__ == '__main__':
    md_to_html_fragment()
    build_full_html()
    embed_images()
    chrome_to_pdf()
    cleanup()
    size = os.path.getsize(PDF) // 1024
    print(f"\nDone!  {PDF}  ({size} KB)")
