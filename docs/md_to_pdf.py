"""
Convert shardora_whitepaper_cn.md → shardora_whitepaper_cn.pdf
Pipeline: markdown → HTML (via pandoc) → PDF (via weasyprint)
"""
import subprocess
import os
import sys
import base64
import re

DOCS_DIR = os.path.dirname(os.path.abspath(__file__))
MD_FILE  = os.path.join(DOCS_DIR, 'shardora_whitepaper_cn.md')
HTML_FILE = os.path.join(DOCS_DIR, '_whitepaper_tmp.html')
PDF_FILE  = os.path.join(DOCS_DIR, 'shardora_whitepaper_cn.pdf')

# ── CSS for professional PDF layout ──
CSS = """
@import url('https://fonts.googleapis.com/css2?family=Noto+Serif+SC:wght@400;700&family=Noto+Sans+SC:wght@400;700&display=swap');

@font-face {
  font-family: 'CJK';
  src: local('Microsoft YaHei'), local('SimHei'), local('Arial Unicode MS');
}

@page {
  size: A4;
  margin: 22mm 20mm 22mm 20mm;
  @bottom-center {
    content: counter(page) " / " counter(pages);
    font-size: 9pt;
    color: #666;
    font-family: 'CJK', sans-serif;
  }
}

* { box-sizing: border-box; }

body {
  font-family: 'CJK', 'Microsoft YaHei', 'SimHei', sans-serif;
  font-size: 10.5pt;
  line-height: 1.75;
  color: #1a1a2e;
  background: #fff;
  max-width: 100%;
}

/* Cover / title area */
h1:first-of-type {
  font-size: 26pt;
  font-weight: 700;
  color: #0f3460;
  text-align: center;
  margin-top: 1.5em;
  margin-bottom: 0.2em;
  page-break-before: avoid;
}

h1 { font-size: 18pt; color: #0f3460; margin-top: 1.4em; border-bottom: 2.5px solid #0f3460; padding-bottom: 4px; }
h2 { font-size: 14pt; color: #16213e; margin-top: 1.2em; border-bottom: 1.5px solid #e94560; padding-bottom: 3px; }
h3 { font-size: 12pt; color: #0f3460; margin-top: 1em; }
h4 { font-size: 11pt; color: #16213e; margin-top: 0.8em; }

/* Subtitle under h1 */
h2:first-of-type {
  font-size: 14pt;
  text-align: center;
  color: #e94560;
  border-bottom: none;
  margin-top: 0.1em;
}

p { margin: 0.5em 0; text-align: justify; }

/* Images — centered, max width */
img {
  display: block;
  max-width: 100%;
  width: 100%;
  margin: 1.2em auto;
  border-radius: 6px;
  border: 1px solid #ccd;
}

/* Image caption (em after img) */
img + em, p:has(img) + p > em {
  display: block;
  text-align: center;
  font-size: 9pt;
  color: #555;
  margin-top: -0.8em;
  margin-bottom: 1em;
}

/* Tables */
table {
  width: 100%;
  border-collapse: collapse;
  font-size: 9.5pt;
  margin: 1em 0;
  page-break-inside: avoid;
}
thead { background: #0f3460; color: #fff; }
th { padding: 6px 10px; text-align: left; }
td { padding: 5px 10px; border-bottom: 1px solid #dde; }
tr:nth-child(even) { background: #f4f6fb; }

/* Code */
code {
  font-family: 'Consolas', 'Courier New', monospace;
  font-size: 8.5pt;
  background: #f0f2f8;
  padding: 1px 4px;
  border-radius: 3px;
  color: #c7254e;
}
pre {
  background: #1a1a2e;
  color: #e0e0e0;
  padding: 10px 14px;
  border-radius: 6px;
  font-size: 8pt;
  overflow: hidden;
  white-space: pre-wrap;
  word-break: break-all;
  line-height: 1.5;
  margin: 0.8em 0;
  page-break-inside: avoid;
}
pre code { background: none; color: #e0e0e0; padding: 0; }

/* Blockquote */
blockquote {
  border-left: 4px solid #e94560;
  margin: 0.8em 0;
  padding: 6px 12px;
  background: #fef6f8;
  color: #555;
  font-style: italic;
  border-radius: 0 4px 4px 0;
}

/* HR */
hr { border: none; border-top: 1.5px solid #dde; margin: 1.2em 0; }

/* Lists */
ul, ol { margin: 0.4em 0 0.4em 1.5em; padding: 0; }
li { margin: 0.2em 0; }

/* Strong / em */
strong { color: #0f3460; font-weight: 700; }
em { color: #555; }

/* TOC links */
a { color: #0f3460; text-decoration: none; }

/* Page breaks */
h1, h2 { page-break-after: avoid; }
table, pre, blockquote { page-break-inside: avoid; }
"""

def embed_images(html_content, base_dir):
    """Replace relative img src with base64 data URIs so PDF is self-contained."""
    def replacer(m):
        src = m.group(1)
        if src.startswith('data:') or src.startswith('http'):
            return m.group(0)
        img_path = os.path.join(base_dir, src)
        if not os.path.exists(img_path):
            print(f'  WARNING: image not found: {img_path}')
            return m.group(0)
        ext = os.path.splitext(src)[1].lower().lstrip('.')
        mime = {'jpg': 'image/jpeg', 'jpeg': 'image/jpeg',
                'png': 'image/png', 'gif': 'image/gif',
                'svg': 'image/svg+xml'}.get(ext, 'image/jpeg')
        with open(img_path, 'rb') as f:
            b64 = base64.b64encode(f.read()).decode()
        print(f'  Embedded: {src} ({os.path.getsize(img_path)//1024}KB)')
        return f'src="data:{mime};base64,{b64}"'
    return re.sub(r'src="([^"]+)"', replacer, html_content)


def step1_md_to_html():
    """Use pandoc to convert markdown → HTML fragment."""
    print('Step 1: pandoc markdown → HTML...')
    result = subprocess.run(
        ['pandoc', MD_FILE, '-f', 'markdown', '-t', 'html5',
         '--standalone', '--metadata', 'title=Shardora白皮书',
         '--wrap=none', '-o', HTML_FILE],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print('pandoc error:', result.stderr)
        sys.exit(1)
    print('  pandoc OK')


def step2_inject_css_and_embed():
    """Read pandoc HTML, inject CSS, embed images as base64."""
    print('Step 2: Inject CSS + embed images...')
    with open(HTML_FILE, 'r', encoding='utf-8') as f:
        html = f.read()

    # Inject CSS into <head>
    style_tag = f'<style>\n{CSS}\n</style>\n'
    if '</head>' in html:
        html = html.replace('</head>', style_tag + '</head>')
    else:
        html = style_tag + html

    # Embed images
    html = embed_images(html, DOCS_DIR)

    with open(HTML_FILE, 'w', encoding='utf-8') as f:
        f.write(html)
    print('  CSS + images OK')


def step3_html_to_pdf():
    """Use weasyprint to render HTML → PDF."""
    print('Step 3: weasyprint HTML → PDF...')
    # Run via subprocess to avoid import issues
    script = f"""
import sys
sys.path.insert(0, r'd:\\work\\SethPub\\.venv\\Lib\\site-packages')
import weasyprint
import logging
logging.getLogger('weasyprint').setLevel(logging.ERROR)
logging.getLogger('fontTools').setLevel(logging.ERROR)
wp = weasyprint.HTML(filename=r'{HTML_FILE}')
wp.write_pdf(r'{PDF_FILE}')
print('PDF written:', r'{PDF_FILE}')
"""
    result = subprocess.run(
        [r'd:\work\SethPub\.venv\Scripts\python.exe', '-c', script],
        capture_output=True, text=True
    )
    print(result.stdout)
    if result.returncode != 0:
        print('weasyprint stderr:', result.stderr[-2000:])
        sys.exit(1)


def cleanup():
    if os.path.exists(HTML_FILE):
        os.remove(HTML_FILE)


if __name__ == '__main__':
    step1_md_to_html()
    step2_inject_css_and_embed()
    step3_html_to_pdf()
    cleanup()
    size_kb = os.path.getsize(PDF_FILE) // 1024
    print(f'\nDone!  {PDF_FILE}  ({size_kb} KB)')
