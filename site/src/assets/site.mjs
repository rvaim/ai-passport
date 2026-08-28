const strings = {
  zh: {
    nav_home: "首页",
    nav_docs: "文档",
    nav_plugins: "插件",
    nav_themes: "主题",
    hero_title: "把小设备变成<br><span>可安装的 AI 平台</span>",
    hero_lede: "面向 ESP32-C3 的轻量插件系统。统一的 UI、主题、BLE 安装和 Lua API，让设备能力可以被安全地扩展。",
    browse_plugins: "浏览插件",
    read_docs: "阅读文档",
    fact_chip: "单核 RISC-V",
    fact_flash: "Flash 存储",
    fact_display: "RGB565 显示",
    fact_runtime: "受限运行时",
    section_platform: "PLATFORM",
    platform_title: "系统服务固定边界，插件自由组合",
    feature_ui_title: "继承式 UI",
    feature_ui_body: "所有组件共享主题样式和系统字体，插件只声明语义，不复制整套视觉属性。",
    feature_storage_title: "私有持久化",
    feature_storage_body: "每个 PAP 拥有隔离的数据空间，异步写入，卸载时一并清理。",
    feature_install_title: "BLE 安装",
    feature_install_body: "浏览器校验设备码后传输 PAP，包校验、进度和设备结果都清晰可见。",
    section_plugins: "PLUGIN LIBRARY",
    plugins_title: "插件",
    plugins_lede: "从仓库中的 Manifest 和 README 自动生成。选择一个插件即可进入安装。",
    section_themes: "THEME LIBRARY",
    themes_title: "主题",
    themes_lede: "主题只覆盖公共样式，不执行代码。切换主题后，系统和插件组件一起变化。",
    section_install: "INSTALL",
    install_title: "准备好设备配对码，就可以开始",
    install_body: "安装页运行在浏览器本地，插件包不会上传。首次连接需要桌面版 Chrome 或 Edge 授权 Web Bluetooth。",
    open_installer: "打开安装页",
    footer_note: "开源硬件与可安装软件平台",
    loading: "正在读取仓库目录…",
    source: "源码",
    readme: "说明",
    download: "下载",
    install: "安装",
    app: "插件",
    theme: "主题",
    api: "API",
    package: "包",
    items: (count) => `${count} 个`,
    catalog_error: "目录读取失败，请稍后重试或直接打开 GitHub。",
  },
  en: {
    nav_home: "Home",
    nav_docs: "Docs",
    nav_plugins: "Plugins",
    nav_themes: "Themes",
    hero_title: "Turn a small device into<br><span>an installable AI platform</span>",
    hero_lede: "A lightweight ESP32-C3 plug-in platform with shared UI, themes, BLE installation, and a bounded Lua API.",
    browse_plugins: "Browse plugins",
    read_docs: "Read docs",
    fact_chip: "single-core RISC-V",
    fact_flash: "Flash storage",
    fact_display: "RGB565 display",
    fact_runtime: "bounded runtime",
    section_platform: "PLATFORM",
    platform_title: "Fixed system boundaries, composable plug-ins",
    feature_ui_title: "Inherited UI",
    feature_ui_body: "Components share theme styles and the system font. Plug-ins declare semantics instead of copying visual tokens.",
    feature_storage_title: "Private persistence",
    feature_storage_body: "Every PAP gets an isolated data space with asynchronous writes and uninstall cleanup.",
    feature_install_title: "BLE installation",
    feature_install_body: "The browser verifies the device code before streaming a PAP with visible package checks and progress.",
    section_plugins: "PLUGIN LIBRARY",
    plugins_title: "Plugins",
    plugins_lede: "Generated from the repository's manifests and READMEs. Choose a plugin to start installation.",
    section_themes: "THEME LIBRARY",
    themes_title: "Themes",
    themes_lede: "Themes override public styles only. They execute no code and change the system and plug-in components together.",
    section_install: "INSTALL",
    install_title: "Have the device pairing code ready",
    install_body: "Installation runs locally in the browser and never uploads the package. Desktop Chrome or Edge is required for Web Bluetooth.",
    open_installer: "Open installer",
    footer_note: "Open hardware and installable software platform",
    loading: "Reading the repository catalog…",
    source: "Source",
    readme: "Readme",
    download: "Download",
    install: "Install",
    app: "Plugin",
    theme: "Theme",
    api: "API",
    package: "Package",
    items: (count) => `${count} item${count === 1 ? "" : "s"}`,
    catalog_error: "The catalog could not be loaded. Try again later or open GitHub directly.",
  },
};

let locale = window.localStorage.getItem("passport-site-locale") === "en" ? "en" : "zh";
let catalog = null;

function translate(key) {
  const value = strings[locale][key];
  return typeof value === "function" ? value : value || key;
}

function applyLocale() {
  document.documentElement.lang = locale === "zh" ? "zh-CN" : "en";
  for (const element of document.querySelectorAll("[data-i18n]")) {
    element.textContent = translate(element.dataset.i18n);
  }
  for (const element of document.querySelectorAll("[data-i18n-html]")) {
    element.innerHTML = translate(element.dataset.i18nHtml);
  }
  const toggle = document.querySelector("#language-toggle");
  if (toggle) toggle.textContent = locale === "zh" ? "English" : "简体中文";
  if (catalog) renderCatalog(catalog);
}

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function link(label, href, className, external = false) {
  const node = element("a", className, label);
  node.href = href;
  if (external) {
    node.target = "_blank";
    node.rel = "noreferrer";
  }
  return node;
}

function packageCard(item) {
  const card = element("article", "package-card");
  const top = element("div", "package-top");
  top.append(element("span", "package-type", item.type === "app" ? translate("app").toUpperCase() : translate("theme").toUpperCase()));
  top.append(element("span", "package-type", item.version));
  card.append(top);
  card.append(element("h3", "", locale === "en" ? item.name_en : item.name));
  card.append(element("code", "package-id", item.id));
  card.append(element("p", "package-description", locale === "en" ? item.description : item.description_zh));

  const meta = element("div", "package-meta");
  meta.append(element("span", "", `${translate("api")} ${item.api}`));
  if (item.runtime) meta.append(element("span", "", item.runtime));
  meta.append(element("span", "", `${translate("package")} ${formatBytes(item.size)}`));
  card.append(meta);

  const actions = element("div", "package-actions");
  if (item.source_url) actions.append(link(translate("source"), item.source_url, "", true));
  if (item.readme_url) actions.append(link(translate("readme"), locale === "en" ? item.readme_url : (item.readme_zh_url || item.readme_url), "", true));
  actions.append(link(translate("download"), item.package_url, ""));
  actions.append(link(translate("install"), item.install_url, "install"));
  card.append(actions);
  return card;
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function renderList(target, items) {
  target.replaceChildren();
  if (!items.length) {
    target.append(element("p", "empty", locale === "en" ? "No packages are available yet." : "暂时没有可用包。"));
    return;
  }
  for (const item of items) target.append(packageCard(item));
}

function renderCatalog(data) {
  const packages = data.packages || [];
  const plugins = packages.filter((item) => item.type === "app");
  const themes = packages.filter((item) => item.type === "theme");
  document.querySelector("#plugin-count").textContent = translate("items")(plugins.length);
  document.querySelector("#theme-count").textContent = translate("items")(themes.length);
  renderList(document.querySelector("#plugin-list"), plugins);
  renderList(document.querySelector("#theme-list"), themes);
}

document.querySelector("#language-toggle")?.addEventListener("click", () => {
  locale = locale === "zh" ? "en" : "zh";
  window.localStorage.setItem("passport-site-locale", locale);
  applyLocale();
});

applyLocale();
fetch("./data/catalog.json", { cache: "no-store" })
  .then((response) => {
    if (!response.ok) throw new Error(`catalog HTTP ${response.status}`);
    return response.json();
  })
  .then((data) => {
    catalog = data;
    renderCatalog(data);
  })
  .catch(() => {
    for (const id of ["plugin-list", "theme-list"]) {
      document.querySelector(`#${id}`).replaceChildren(element("p", "empty", translate("catalog_error")));
    }
  });
