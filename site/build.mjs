#!/usr/bin/env node

import { createHash } from "node:crypto";
import { cp, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { marked } from "marked";

const SITE_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(SITE_DIR, "..");

function parseArgs(argv) {
  const result = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (!argument.startsWith("--")) throw new Error(`未知参数：${argument}`);
    const key = argument.slice(2).replaceAll("-", "_");
    const value = argv[index + 1];
    if (!value || value.startsWith("--")) throw new Error(`参数 ${argument} 缺少值`);
    result[key] = value;
    index += 1;
  }
  return result;
}

function relativeHref(fromFile, toFile) {
  const relative = path.relative(path.dirname(fromFile), toFile).replaceAll(path.sep, "/");
  return relative || path.basename(toFile);
}

function htmlEscape(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function titleFromMarkdown(markdown, fallback) {
  const match = markdown.match(/^#\s+(.+)$/m);
  return match ? match[1].trim() : fallback;
}

function githubUrl(repository, ref, relativePath) {
  return `https://github.com/${repository}/blob/${ref}/${relativePath}`;
}

function rewriteMarkdownLinks(html, sourceRelative, outputRelative, repository, ref) {
  return html.replace(/href="([^"]+)"/g, (_whole, rawHref) => {
    const match = rawHref.match(/^([^?#]*)([?#].*)?$/);
    const rawPath = match?.[1] || "";
    const suffix = match?.[2] || "";
    if (!rawPath || /^(?:https?:|mailto:|tel:|data:|#)/i.test(rawHref)) {
      return `href="${rawHref}"`;
    }
    const sourceTarget = path.normalize(path.join(path.dirname(sourceRelative), rawPath));
    const target = sourceTarget.replaceAll(path.sep, "/");
    if (target.startsWith("docs/") && target.endsWith(".md")) {
      const outputTarget = `${target.slice(0, -3)}.html`;
      const href = `${relativeHref(outputRelative, outputTarget)}${suffix}`;
      return `href="${href}"`;
    }
    if (target === "web/installer.html") {
      return `href="${relativeHref(outputRelative, "tools/installer/installer.html")}${suffix}"`;
    }
    return `href="${githubUrl(repository, ref, target)}${suffix}"`;
  });
}

function renderSiteShell({
  title,
  lang,
  assetPrefix,
  rootPrefix,
  repository,
  peerHref,
  peerLabel,
  content,
}) {
  return `<!doctype html>
<html lang="${lang}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#d9edf5">
  <meta name="site-root" content="${rootPrefix}">
  <title>${htmlEscape(title)} · AI Passport</title>
  <link rel="stylesheet" href="${assetPrefix}assets/site.css">
</head>
<body class="document-page">
  <header class="site-header">
    <a class="brand" href="${rootPrefix}index.html" aria-label="AI Passport 首页">
      <span class="brand-mark" aria-hidden="true">P</span>
      <span>AI Passport</span>
    </a>
    <nav class="site-nav" aria-label="主导航">
      <a href="${rootPrefix}index.html">首页</a>
      <a href="${rootPrefix}docs/index.html">文档</a>
      <a href="${rootPrefix}index.html#plugins">插件</a>
      <a href="${rootPrefix}index.html#themes">主题</a>
      <a href="https://github.com/${repository}" target="_blank" rel="noreferrer">GitHub</a>
      <a class="language-link" href="${peerHref}">${peerLabel}</a>
    </nav>
  </header>
  <main class="document-main">
    ${content}
  </main>
  <footer class="site-footer">
    <span>FoloToy AI Passport · ESP32-C3 / ESP-IDF 5.5.3</span>
    <a href="${rootPrefix}index.html">返回首页</a>
  </footer>
</body>
</html>
`;
}

async function listMarkdownFiles(directory) {
  const entries = [];
  for (const item of await readdir(directory, { withFileTypes: true })) {
    const itemPath = path.join(directory, item.name);
    if (item.isDirectory()) entries.push(...await listMarkdownFiles(itemPath));
    else if (item.isFile() && item.name.endsWith(".md")) entries.push(itemPath);
  }
  return entries.sort();
}

function docsOutputPath(output, source) {
  const relative = path.relative(path.join(REPO_ROOT, "docs"), source).replaceAll(path.sep, "/");
  return path.join(output, "docs", `${relative.slice(0, -3)}.html`);
}

function languagePeer(source, output) {
  const isChinese = source.endsWith(".zh_CN.md");
  const peerSource = isChinese
    ? `${source.slice(0, -".zh_CN.md".length)}.md`
    : `${source.slice(0, -".md".length)}.zh_CN.md`;
  return {
    href: relativeHref(docsOutputPath(output, source), docsOutputPath(output, peerSource)),
    label: isChinese ? "English" : "简体中文",
    lang: isChinese ? "zh-CN" : "en",
  };
}

function docCategory(relative) {
  const first = relative.split("/")[0];
  return {
    contribution: "贡献与协作",
    development: "开发与构建",
    "hardware-design": "硬件设计",
    "software-design": "软件设计",
    platform: "Passport 平台",
  }[first] || "项目资料";
}

async function buildDocs(output, repository, ref) {
  const docsRoot = path.join(REPO_ROOT, "docs");
  const markdownFiles = await listMarkdownFiles(docsRoot);
  const pages = [];
  for (const source of markdownFiles) {
    const markdown = await readFile(source, "utf8");
    const outputPath = docsOutputPath(output, source);
    const outputRelative = path.relative(output, outputPath).replaceAll(path.sep, "/");
    const assetPrefix = "../".repeat(outputRelative.split("/").length - 1);
    const peer = languagePeer(source, output);
    const html = rewriteMarkdownLinks(
      marked.parse(markdown),
      path.relative(REPO_ROOT, source).replaceAll(path.sep, "/"),
      outputRelative,
      repository,
      ref,
    );
    const title = titleFromMarkdown(markdown, path.basename(source, ".md"));
    const sourceLink = githubUrl(
      repository,
      ref,
      path.relative(REPO_ROOT, source).replaceAll(path.sep, "/"),
    );
    const content = `<article class="document-card markdown-body">
      <div class="document-toolbar">
        <a href="${sourceLink}" target="_blank" rel="noreferrer">查看源文件</a>
        <a href="${peer.href}">${peer.label}</a>
      </div>
      ${html}
    </article>`;
    await mkdir(path.dirname(outputPath), { recursive: true });
    await writeFile(
      outputPath,
      renderSiteShell({
        title,
        lang: peer.lang,
        assetPrefix,
        rootPrefix: assetPrefix,
        repository,
        peerHref: peer.href,
        peerLabel: peer.label,
        content,
      }),
      "utf8",
    );
    if (!source.endsWith(".zh_CN.md")) {
      pages.push({
        category: docCategory(path.relative(docsRoot, source).replaceAll(path.sep, "/")),
        title,
        href: path.relative(output, outputPath).replaceAll(path.sep, "/"),
      });
    }
  }

  const grouped = new Map();
  for (const page of pages) {
    if (!grouped.has(page.category)) grouped.set(page.category, []);
    grouped.get(page.category).push(page);
  }
  const groups = [...grouped.entries()].map(([category, items]) => `<section class="doc-group">
    <h2>${htmlEscape(category)}</h2>
    <div class="doc-links">${items.map((item) => `<a href="../${item.href}">${htmlEscape(item.title)}</a>`).join("")}</div>
  </section>`).join("");
  const indexPath = path.join(output, "docs", "index.html");
  await mkdir(path.dirname(indexPath), { recursive: true });
  await writeFile(indexPath, renderSiteShell({
    title: "文档",
    lang: "zh-CN",
    assetPrefix: "../",
    rootPrefix: "../",
    repository,
    peerHref: "../docs/INDEX.html",
    peerLabel: "English",
    content: `<section class="docs-hero">
      <p class="eyebrow">PASSPORT PLATFORM</p>
      <h1>文档中心</h1>
      <p>从硬件约束、系统架构到插件开发和发布流程，所有说明都来自仓库中的权威文档。</p>
    </section>${groups}`,
  }), "utf8");
}

function releaseAssetUrl(repository, tag, asset) {
  return `https://github.com/${repository}/releases/download/${encodeURIComponent(tag)}/${encodeURIComponent(asset)}`;
}

async function buildSite(args) {
  const output = path.resolve(args.output || path.join(REPO_ROOT, "site-dist"));
  const catalogPath = path.resolve(args.catalog);
  const packagesPath = path.resolve(args.packages);
  const repository = args.repository || process.env.GITHUB_REPOSITORY || "rvaim/ai-passport";
  const ref = args.ref || process.env.GITHUB_SHA || "main";
  const releaseTag = args.release_tag || process.env.RELEASE_TAG || "latest";

  const catalog = JSON.parse(await readFile(catalogPath, "utf8"));
  if (catalog.schema !== 1 || !Array.isArray(catalog.packages)) {
    throw new Error("catalog.json schema 无效");
  }
  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  await cp(path.join(SITE_DIR, "src"), output, { recursive: true });
  await writeFile(path.join(output, ".nojekyll"), "", "utf8");

  const enriched = {
    schema: catalog.schema,
    source: { repository, ref, release: releaseTag },
    packages: catalog.packages.map((item) => ({
      ...item,
      package_url: `./packages/${item.asset}`,
      install_url: `./tools/installer/installer.html?package=${encodeURIComponent(`../../packages/${item.asset}`)}`,
      release_url: releaseAssetUrl(repository, releaseTag, item.asset),
      source_url: githubUrl(repository, ref, item.source),
      readme_url: item.readme ? githubUrl(repository, ref, item.readme) : null,
      readme_zh_url: item.readme_zh ? githubUrl(repository, ref, item.readme_zh) : null,
    })),
  };
  await mkdir(path.join(output, "data"), { recursive: true });
  await writeFile(path.join(output, "data", "catalog.json"), `${JSON.stringify(enriched, null, 2)}\n`, "utf8");

  const sitePackages = path.join(output, "packages");
  await mkdir(sitePackages, { recursive: true });
  for (const item of enriched.packages) {
    const sourcePackage = path.join(packagesPath, item.asset);
    const targetPackage = path.join(sitePackages, item.asset);
    const data = await readFile(sourcePackage);
    const digest = createHash("sha256").update(data).digest("hex");
    if (data.length !== item.size || digest !== item.sha256) {
      throw new Error(`PAP 校验失败: ${item.asset}`);
    }
    await writeFile(targetPackage, data);
  }

  const installerOutput = path.join(output, "tools", "installer");
  await mkdir(installerOutput, { recursive: true });
  for (const file of ["installer.html", "installer.mjs", "passport-install-protocol.mjs"]) {
    await cp(path.join(REPO_ROOT, "web", file), path.join(installerOutput, file));
  }
  await buildDocs(output, repository, ref);
  console.log(`Site build: PASS (${enriched.packages.length} packages, ${output})`);
}

try {
  const args = parseArgs(process.argv.slice(2));
  if (!args.catalog || !args.packages) {
    throw new Error("需要 --catalog 和 --packages");
  }
  await buildSite(args);
} catch (error) {
  console.error(`ERROR: ${error.message}`);
  process.exitCode = 1;
}
