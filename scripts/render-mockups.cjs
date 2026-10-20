// SPDX-License-Identifier: GPL-3.0-or-later
// Render the editable repository SVGs to self-contained PNGs.
const fs = require("node:fs/promises");
const path = require("node:path");
const sharp = require("sharp");

async function main() {
  const root = path.resolve(__dirname, "../docs/mockups");
  for (const name of (await fs.readdir(root)).filter((name) => name.endsWith(".svg")).sort()) {
    const source = path.join(root, name);
    const output = source.replace(/\.svg$/, ".png");
    await sharp(source, { density: 72 })
      .png({ compressionLevel: 9 })
      .toFile(output);
    const { width, height } = await sharp(output).metadata();
    console.log(`${path.basename(output)}: ${width} × ${height}`);
  }
}

main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
