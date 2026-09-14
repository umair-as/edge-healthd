// Bundle-size gate: fails when the gzipped build exceeds the budget.
// The UI is embedded into the Go server binary that runs on the gateway, so
// every kilobyte ships to the device.
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { gzipSync } from 'node:zlib';

const BUDGET_GZIP_KB = 50;
const dist = new URL('../dist/', import.meta.url).pathname;

function walk(dir) {
  return readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    return statSync(path).isDirectory() ? walk(path) : [path];
  });
}

let files;
try {
  files = walk(dist).filter((f) => /\.(js|css|html)$/.test(f));
} catch {
  console.error('dist/ not found — run `npm run build` first.');
  process.exit(1);
}

let total = 0;
for (const file of files) {
  const gz = gzipSync(readFileSync(file), { level: 9 }).length;
  total += gz;
  console.log(`${(gz / 1024).toFixed(2).padStart(8)} kB  ${relative(dist, file)}`);
}

const totalKb = total / 1024;
console.log(`${totalKb.toFixed(2).padStart(8)} kB  total gzip (budget ${BUDGET_GZIP_KB} kB)`);
if (totalKb > BUDGET_GZIP_KB) {
  console.error(`Bundle exceeds the ${BUDGET_GZIP_KB} kB gzip budget.`);
  process.exit(1);
}
