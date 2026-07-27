const fs = require('fs');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..', '..');
const fontsPath = path.join(repoRoot, 'src', 'vario', 'ui', 'display', 'fonts.h');
const outPath = path.join(__dirname, 'leaf-fonts.js');
const source = fs.readFileSync(fontsPath, 'utf8');

function decodeCStringLiteral(text) {
  const bytes = [];
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch !== '\\') {
      bytes.push(ch.charCodeAt(0) & 0xff);
      continue;
    }
    i++;
    const esc = text[i];
    if (esc >= '0' && esc <= '7') {
      let oct = esc;
      for (let n = 0; n < 2 && i + 1 < text.length && text[i + 1] >= '0' && text[i + 1] <= '7'; n++) {
        oct += text[++i];
      }
      bytes.push(parseInt(oct, 8) & 0xff);
    } else if (esc === 'x') {
      let hex = '';
      while (i + 1 < text.length && /[0-9a-fA-F]/.test(text[i + 1])) hex += text[++i];
      bytes.push(parseInt(hex || '0', 16) & 0xff);
    } else {
      const escapes = { n: 10, r: 13, t: 9, b: 8, f: 12, v: 11, '\\': 92, '"': 34, "'": 39, '0': 0 };
      bytes.push((escapes[esc] ?? esc.charCodeAt(0)) & 0xff);
    }
  }
  return bytes;
}

function extractFonts() {
  const fonts = {};
  const declRe = /const\s+uint8_t\s+(\w+)\[(\d+)\]\s+U8G2_FONT_SECTION\("[^"]+"\)\s*=/g;
  let match;
  while ((match = declRe.exec(source))) {
    const [, name, sizeText] = match;
    const size = Number(sizeText);
    const nextDecl = source.slice(declRe.lastIndex).search(/\n(?:\/\/[^\n]*\n)*const\s+uint8_t\s+\w+\[/);
    const end = nextDecl < 0 ? source.indexOf('#endif', declRe.lastIndex) : declRe.lastIndex + nextDecl;
    const initializer = source.slice(declRe.lastIndex, end < 0 ? source.length : end);
    let bytes = [];
    for (const stringMatch of initializer.matchAll(/"((?:\\.|[^"\\])*)"/g)) {
      bytes = bytes.concat(decodeCStringLiteral(stringMatch[1]));
    }
    if (bytes.length === size - 1) bytes.push(0);
    if (bytes.length !== size) {
      throw new Error(`${name}: decoded ${bytes.length} bytes, expected ${size}`);
    }
    fonts[name] = bytes;
  }
  return fonts;
}

function formatArray(bytes) {
  const rows = [];
  for (let i = 0; i < bytes.length; i += 24) rows.push(`    ${bytes.slice(i, i + 24).join(', ')}`);
  return `[\n${rows.join(',\n')}\n  ]`;
}

const fonts = extractFonts();
const body = Object.entries(fonts)
  .map(([name, bytes]) => `  ${JSON.stringify(name)}: ${formatArray(bytes)}`)
  .join(',\n');

fs.writeFileSync(outPath, `// Generated from ../../src/vario/ui/display/fonts.h by generate_leaf_fonts.js\nwindow.LEAF_U8G2_FONTS = {\n${body}\n};\n`);
console.log(`Wrote ${Object.keys(fonts).length} fonts to ${outPath}`);
