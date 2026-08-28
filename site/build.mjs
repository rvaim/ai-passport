#!/usr/bin/env node

import { copyFile, mkdir, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SITE_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SITE_DIR, "..");

function parseArgs(argv) {
  const result = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument !== "--output") throw new Error(`未知参数：${argument}`);
    const value = argv[index + 1];
    if (!value || value.startsWith("--")) throw new Error("参数 --output 缺少值");
    result.output = value;
    index += 1;
  }
  return result;
}

async function buildSite(outputArgument) {
  const output = path.resolve(outputArgument || path.join(REPO_ROOT, "site-dist"));
  const webRoot = path.join(REPO_ROOT, "web");

  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  await copyFile(path.join(webRoot, "installer.html"), path.join(output, "index.html"));
  await copyFile(path.join(webRoot, "installer.mjs"), path.join(output, "installer.mjs"));
  await copyFile(
    path.join(webRoot, "passport-install-protocol.mjs"),
    path.join(output, "passport-install-protocol.mjs"),
  );
  await writeFile(path.join(output, ".nojekyll"), "", "utf8");
  console.log(`Site build: PASS (unified installer, ${output})`);
}

try {
  const args = parseArgs(process.argv.slice(2));
  await buildSite(args.output);
} catch (error) {
  console.error(`ERROR: ${error.message}`);
  process.exitCode = 1;
}
