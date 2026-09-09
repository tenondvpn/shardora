/**
 * Convert whitepaper HTML → PDF using Chrome headless
 * Usage: node print_pdf.js <input.html> <output.pdf>
 */
const { execSync, spawnSync } = require('child_process');
const path = require('path');
const fs   = require('fs');

const htmlFile = path.resolve(process.argv[2]);
const pdfFile  = path.resolve(process.argv[3]);
const chrome   = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';

if (!fs.existsSync(htmlFile)) {
  console.error('HTML file not found:', htmlFile);
  process.exit(1);
}

console.log('Chrome headless → PDF...');
console.log('  Input :', htmlFile);
console.log('  Output:', pdfFile);

const args = [
  '--headless=new',
  '--disable-gpu',
  '--no-sandbox',
  '--disable-web-security',
  '--allow-file-access-from-files',
  '--run-all-compositor-stages-before-draw',
  '--print-to-pdf-no-header',
  `--print-to-pdf=${pdfFile}`,
  `--virtual-time-budget=10000`,
  `file:///${htmlFile.replace(/\\/g, '/')}`
];

const result = spawnSync(`"${chrome}"`, args, {
  shell: true,
  stdio: ['ignore', 'pipe', 'pipe'],
  timeout: 60000
});

if (result.error) {
  console.error('Spawn error:', result.error);
  process.exit(1);
}

if (!fs.existsSync(pdfFile)) {
  console.error('PDF not created. stderr:', result.stderr?.toString().slice(0, 500));
  process.exit(1);
}

const size = (fs.statSync(pdfFile).size / 1024).toFixed(0);
console.log(`Done! ${pdfFile} (${size} KB)`);
