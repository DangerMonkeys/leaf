// Run with Node from the repository root, after manual-captures. Uses website's sharp.
import sharp from '../../website/node_modules/sharp/lib/index.js';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';

const destination = 'docs/user-guides/vario-user-manual/release-screens';
await mkdir(destination, { recursive: true });
const records = [];
for (const name of ['thermal-core', 'thermal-track', 'navigate', 'logging-menu',
  ...Array.from({ length: 13 }, (_, i) => `nav-field-${i}`)]) {
  const source = `sim/build/manual-raw/${name}.png`;
  const { data, info } = await sharp(source).removeAlpha().raw().toBuffer({ resolveWithObject: true });
  if (info.width !== 96 || info.height !== 192) throw new Error(`Unexpected frame: ${name}`);
  const pixels = Buffer.alloc(96 * 192);
  for (let i = 0; i < pixels.length; ++i) pixels[i] = data[i * info.channels] < 128 ? 0 : 255;
  let output = sharp(pixels, { raw: { width: 96, height: 192, channels: 1 } });
  const crop = name.startsWith('nav-field-') ? { left: 3, top: 134, width: 46, height: 21 } : null;
  if (crop) output = output.extract(crop);
  const png = await output.png().toBuffer();
  await writeFile(`${destination}/${name}.png`, png);
  records.push({ name, source, crop, sha256: createHash('sha256').update(png).digest('hex') });
}
const navStrip = await sharp(`${destination}/navigate.png`).extract({ left: 18, top: 112, width: 78, height: 22 }).png().toBuffer();
await writeFile(`${destination}/nav-destination.png`, navStrip);
records.push({ name: 'nav-destination', source: 'navigate.png', crop: { left: 18, top: 112, width: 78, height: 22 }, sha256: createHash('sha256').update(navStrip).digest('hex') });
for (const name of ['uploading-2', 'cancel-confirmation', 'compact-both-success', 'compact-both-problem']) {
  const source = `sim/build/manual-charging/${name}.png`;
  const { data, info } = await sharp(source).removeAlpha().raw().toBuffer({ resolveWithObject: true });
  const pixels = Buffer.alloc(info.width * info.height);
  for (let i = 0; i < pixels.length; ++i) pixels[i] = data[i * info.channels] < 128 ? 0 : 255;
  const crop = name.startsWith('compact-') ? { left: 0, top: 150, width: 96, height: 23 } : { left: 0, top: 141, width: 96, height: 51 };
  const png = await sharp(pixels, { raw: { width: info.width, height: info.height, channels: 1 } }).extract(crop).png().toBuffer();
  await writeFile(`${destination}/charging-${name}.png`, png);
  records.push({ name: `charging-${name}`, source, crop, sha256: createHash('sha256').update(png).digest('hex') });
}
await writeFile(`${destination}/manifest.json`, JSON.stringify({
  sourceHead: execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
  firmwareBase: execFileSync('git', ['rev-parse', 'origin/main'], { encoding: 'utf8' }).trim(),
  generator: 'sim/manual/capture.cpp',
  generatorSha256: createHash('sha256').update(await readFile('sim/manual/capture.cpp')).digest('hex'),
  simulatorSha256: createHash('sha256').update(await readFile('sim/build/leafsim')).digest('hex'),
  captureExecutableSha256: createHash('sha256').update(await readFile('sim/build/manual_capture')).digest('hex'),
  chargingExecutableSha256: createHash('sha256').update(await readFile('sim/build/charging_dialogs_test')).digest('hex'),
  firmwareHasUncommittedChanges: Boolean(execFileSync('git', ['diff', '--name-only', 'HEAD', '--', 'src/vario', 'src/variants'], { encoding: 'utf8' }).trim()),
  palette: 'LCD on -> #000000; LCD off -> #ffffff; no interpolation or pixel edits',
  workload: 'thermal-core.json at 237/407 seconds; synthetic 3-cylinder route at 1500 m, elevation 800 m, sink 1 m/s; developer mode off',
  records,
}, null, 2) + '\n');
