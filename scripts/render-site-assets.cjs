// SPDX-License-Identifier: GPL-3.0-or-later
// Optional artwork maintenance; GitHub Pages publishes the committed PNGs.
const path = require("node:path");
const sharp = require("sharp");
const assets = path.resolve(__dirname, "../site/assets");

async function main() {
  await sharp(path.join(assets, "social-ps5-studio-v1.svg"), { density: 72 })
    .png({ compressionLevel: 9 })
    .toFile(path.join(assets, "social-ps5-studio-v1.png"));
  for (const [size, name] of [[96, "favicon-96.png"], [180, "apple-touch-icon.png"]]) {
    await sharp(path.join(assets, "mark.svg"), { density: 288 })
      .resize(size, size)
      .png({ compressionLevel: 9 })
      .toFile(path.join(assets, name));
  }
  console.log("Rendered the 1200 × 630 social card and 96/180 px icons.");
}

main().catch((error) => {
  console.error(error.message);
  process.exitCode = 1;
});
