"""
Build shardora_whitepaper_cn.pdf
Pipeline: Markdown → pandoc → HTML (CSS + base64 images) → Chrome headless → PDF
"""
import subprocess, os, sys, base64, re

DOCS  = os.path.dirname(os.path.abspath(__file__))
MD    = os.path.join(DOCS, 'shardora_whitepaper_cn.md')
HTML  = os.path.join(DOCS, '_wp_tmp.html')
PDF   = os.path.join(DOCS, 'shardora_whitepaper_cn.pdf')
CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'

# ── Professional A4 CSS ─────────────────────────────────────────────
CSS = r"""
@font-face { font-family:'CJK'; src:local('Microsoft YaHei'),local('SimHei'); }
@page {
  size: A4;
  margin: 20mm 18mm 22mm 18mm;
  @bottom-center { content: counter(page) " / " counter(pages);
    font-size:8pt; color:#888; font-family:'CJK',sans-serif; }
}
*{box-sizing:border-box;}
body{
  font-family:'CJK','Microsoft YaHei','SimHei',sans-serif;
  font-size:10.5pt; line-height:1.8; color:#111827; background:#fff;
  max-width:100%;
}
h1{font-size:22pt;font-weight:700;color:#0f3460;text-align:center;
   margin:1.2em 0 0.2em; border-bottom:3px solid #0f3460; padding-bottom:6px;}
h2{font-size:14pt;color:#0f3460;margin-top:1.4em;
   border-bottom:2px solid #e94560;padding-bottom:3px; page-break-after:avoid;}
h3{font-size:12pt;color:#16213e;margin-top:1.1em; page-break-after:avoid;}
h4{font-size:11pt;color:#0f3460;margin-top:.9em;}
p{margin:.45em 0; text-align:justify;}
img{display:block;max-width:100%;width:100%;margin:1em auto;
    border-radius:5px;border:1px solid #dde; page-break-inside:avoid;}
p:has(img)+p>em, p>em:only-child{display:block;text-align:center;
    font-size:8.5pt;color:#666;margin-top:-0.6em;margin-bottom:.9em;}
table{width:100%;border-collapse:collapse;font-size:9pt;
      margin:.8em 0;page-break-inside:avoid;}
thead{background:#0f3460;color:#fff;}
th{padding:6px 9px;text-align:left;}
td{padding:4px 9px;border-bottom:1px solid #e2e8f0;}
tr:nth-child(even){background:#f8fafc;}
code{font-family:'Consolas','Courier New',monospace;font-size:8.5pt;
     background:#f1f5f9;padding:1px 4px;border-radius:3px;color:#c7254e;}
pre{background:#1e293b;color:#e2e8f0;padding:9px 13px;border-radius:6px;
    font-size:7.8pt;white-space:pre-wrap;word-break:break-all;
    line-height:1.55;margin:.7em 0;page-break-inside:avoid;}
pre code{background:none;color:#e2e8f0;padding:0;}
blockquote{border-left:4px solid #e94560;margin:.7em 0;padding:5px 11px;
           background:#fff5f7;color:#555;font-style:italic;border-radius:0 4px 4px 0;}
hr{border:none;border-top:1.5px solid #e2e8f0;margin:1em 0;}
ul,ol{margin:.4em 0 .4em 1.5em;padding:0;}
li{margin:.18em 0;}
strong{color:#0f3460;font-weight:700;}
a{color:#0f3460;text-decoration:none;}
h1,h2,h3{page-break-after:avoid;}
"""

# ── Step 1: markdown → HTML via pandoc ────────────────────────────
def md_to_html():
    print("Step 1  pandoc markdown → HTML")
    r = subprocess.run(
        ['pandoc', MD, '-f','markdown', '-t','html5',
         '--standalone', '--wrap=none',
         '--metadata', 'title=Shardora白皮书', '-o', HTML],
        capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"pandoc error:\n{r.stderr}")
    print("        OK")

# ── Step 2: inject CSS + embed images ─────────────────────────────
def inject_and_embed():
    print("Step 2  inject CSS + embed images as base64")
    html = open(HTML, encoding='utf-8').read()

    # inject CSS
    html = html.replace('</head>', f'<style>\n{CSS}\n</style>\n</head>')

    # embed images
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
        kb   = os.path.getsize(path)//1024
        print(f"        embedded {src}  ({kb} KB)")
        return f'src="data:{mime};base64,{b64}"'

    html = re.sub(r'src="([^"]+)"', embed, html)
    open(HTML, 'w', encoding='utf-8').write(html)
    print("        OK")

# ── Step 3: Chrome headless → PDF ─────────────────────────────────
def chrome_to_pdf():
    print("Step 3  Chrome headless → PDF")
    file_url = 'file:///' + HTML.replace('\\', '/')
    args = [
        CHROME,
        '--headless=new',
        '--disable-gpu',
        '--no-sandbox',
        '--disable-web-security',
        '--allow-file-access-from-files',
        '--run-all-compositor-stages-before-draw',
        '--virtual-time-budget=15000',
        '--print-to-pdf-no-header',
        f'--print-to-pdf={PDF}',
        file_url,
    ]
    r = subprocess.run(args, capture_output=True, text=True, timeout=90)
    if not os.path.exists(PDF):
        sys.exit(f"Chrome failed to produce PDF.\nstderr: {r.stderr[:800]}")
    print(f"        OK  →  {PDF}  ({os.path.getsize(PDF)//1024} KB)")

def cleanup():
    if os.path.exists(HTML):
        os.remove(HTML)

if __name__ == '__main__':
    md_to_html()
    inject_and_embed()
    chrome_to_pdf()
    cleanup()
    size = os.path.getsize(PDF) // 1024
    print(f"\nDone!  {PDF}  ({size} KB)")
