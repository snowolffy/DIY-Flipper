// png_to_pic.js - cuts a rectangle out of a 1:1 mockup PNG into an RGB565 picture header in the same format
// Flipper UI Studio exports (black = transparent, so it can be drawn over any background).
// usage: node tools/png_to_pic.js in.png NAME x y w h out.h
const zlib = require('zlib'), fs = require('fs');
function decode(file) {
  const b = fs.readFileSync(file); let p = 8, w, h, ct, idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p), type = b.toString('ascii', p + 4, p + 8), d = b.slice(p + 8, p + 8 + len);
    if (type === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; }
    else if (type === 'IDAT') idat.push(d);
    p += 12 + len;
  }
  const bpp = ct === 6 ? 4 : 3, raw = zlib.inflateSync(Buffer.concat(idat)), out = [];
  let prev = Buffer.alloc(w * bpp);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (w * bpp + 1)], s = raw.slice(y * (w * bpp + 1) + 1, (y + 1) * (w * bpp + 1)), line = Buffer.alloc(w * bpp);
    for (let i = 0; i < w * bpp; i++) {
      const a = i >= bpp ? line[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0; let v = s[i];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const pp = a + up - c, pa = Math.abs(pp - a), pb = Math.abs(pp - up), pc = Math.abs(pp - c); v += (pa <= pb && pa <= pc) ? a : pb <= pc ? up : c; }
      line[i] = v & 255;
    }
    for (let x = 0; x < w; x++) out.push([line[x * bpp], line[x * bpp + 1], line[x * bpp + 2], bpp === 4 ? line[x * bpp + 3] : 255]);
    prev = line;
  }
  return { w, h, px: out };
}
const [inp, name, x0, y0, w, h, outp] = process.argv.slice(2);
const img = decode(inp), vals = [];
for (let y = +y0; y < +y0 + +h; y++) for (let x = +x0; x < +x0 + +w; x++) {
  const [r, g, b, a] = img.px[y * img.w + x];
  const c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  vals.push(a < 128 || c === 0 ? 0xF81F : c);
}
let s = `// ${require('path').basename(outp)} - cut from ${require('path').basename(inp)} (x${x0} y${y0} ${w}x${h}) by tools/png_to_pic.js\n`;
s += `// Picture ${w}x${h} - RGB565, idx=y*w+x, 0xF81F = transparent\n#pragma once\n\n#include <Arduino.h>\n\n`;
s += `const uint16_t PIC_${name}[${vals.length}] PROGMEM = {\n`;
for (let i = 0; i < vals.length; i += 12) s += '  ' + vals.slice(i, i + 12).map(v => '0x' + v.toString(16).toUpperCase().padStart(4, '0')).join(', ') + ',\n';
s += '};\n';
fs.writeFileSync(outp, s);
