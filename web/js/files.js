import { Unzip, UnzipInflate } from "../vendor/fflate.js";

export const STORE = "store";
export const LIST = "list";
export const IGNORE = "ignore";

const IMAGE_FOLDERS = new Set(["characters", "faces", "parallaxes", "pictures", "system", "titles1"]);
const MAX_LISTED = 20000;

export function classifyPath(path) {
  const parts = path.toLowerCase().split("/").filter(Boolean);
  const name = parts[parts.length - 1];
  const www = parts.lastIndexOf("www");

  if (www >= 0) {
    const [top, second] = parts.slice(www + 1);
    const depth = parts.length - www - 1;

    if (top === "js" && depth === 2 && (second === "main.js" || second === "plugins.js")) return STORE;
    if (top === "data" && depth > 1) return STORE;
    if (top === "img" && IMAGE_FOLDERS.has(second) && depth > 2) return STORE;
    if (top === "audio" && depth > 2) return STORE;
    if (top === "fonts" && depth === 2 && name.endsWith(".ttf")) return STORE;
  }

  if (/\.(ini|nfo)$/.test(name)) return STORE;
  if (/^steam(client|_api).*\.dll$/.test(name)) return STORE;
  if (parts.includes("steam_settings")) return STORE;
  if (parts.length <= 3 && /\.(txt|url)$/.test(name)) return STORE;

  if (www >= 0 && www < parts.length - 1) return IGNORE;
  return LIST;
}

export function sortFiles(files) {
  const stored = [];
  const listed = [];

  for (const file of files) {
    const kind = classifyPath(file.path);
    if (kind === STORE) stored.push(file);
    if (kind === LIST && listed.length < MAX_LISTED) listed.push(file.path);
  }

  return { stored, listed };
}

export function filesFromInput(fileList) {
  return [...fileList].map((file) => ({ path: file.webkitRelativePath || file.name, blob: file }));
}

function readDirectoryBatch(reader) {
  return new Promise((resolve, reject) => reader.readEntries(resolve, reject));
}

function fileOfEntry(entry) {
  return new Promise((resolve, reject) => entry.file(resolve, reject));
}

async function walk(entry, prefix, out, onProgress) {
  const path = prefix ? `${prefix}/${entry.name}` : entry.name;

  if (entry.isFile) {
    out.push({ path, blob: await fileOfEntry(entry) });
    if (out.length % 200 === 0) onProgress(out.length);
    return;
  }

  const reader = entry.createReader();
  for (;;) {
    const batch = await readDirectoryBatch(reader);
    if (batch.length === 0) break;
    for (const child of batch) await walk(child, path, out, onProgress);
  }
}

export async function filesFromDrop(items, onProgress) {
  const entries = [...items].map((item) => item.webkitGetAsEntry()).filter(Boolean);
  const out = [];
  for (const entry of entries) await walk(entry, "", out, onProgress);
  return out;
}

export function isZip(file) {
  return file.name.toLowerCase().endsWith(".zip");
}

export async function filesFromZip(zipFile, onProgress) {
  const out = [];
  const pending = [];

  const unzip = new Unzip((entry) => {
    const path = entry.name.replace(/\\/g, "/");
    if (path.endsWith("/")) return;

    const kind = classifyPath(path);
    if (kind === IGNORE) return;
    if (kind === LIST) {
      out.push({ path, blob: new Blob() });
      return;
    }

    const chunks = [];
    pending.push(
      new Promise((resolve, reject) => {
        entry.ondata = (error, chunk, final) => {
          if (error) return reject(error);
          chunks.push(chunk);
          if (final) {
            out.push({ path, blob: new Blob(chunks) });
            resolve();
          }
        };
      }),
    );
    entry.start();
  });
  unzip.register(UnzipInflate);

  const reader = zipFile.stream().getReader();
  let read = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    unzip.push(value);
    read += value.length;
    onProgress(read / zipFile.size);
  }
  unzip.push(new Uint8Array(0), true);
  await Promise.all(pending);

  return out;
}

export function missingGameParts(paths) {
  const lower = paths.map((path) => path.toLowerCase());
  const has = (pattern) => lower.some((path) => pattern.test(path));
  const required = [
    ["www/js/main.js", /(^|\/)www\/js\/main\.js$/],
    ["www/data", /(^|\/)www\/data\//],
    ["www/img/pictures", /(^|\/)www\/img\/pictures\//],
    ["www/audio", /(^|\/)www\/audio\//],
    ["www/fonts/Faustina-Regular.ttf", /(^|\/)www\/fonts\/faustina-regular\.ttf$/],
  ];
  return required.filter(([, pattern]) => !has(pattern)).map(([name]) => name);
}

export async function readGameVersion(files) {
  const main = files.find((file) => /(^|\/)www\/js\/main\.js$/i.test(file.path));
  if (!main) return "unknown";
  const match = (await main.blob.text()).match(/GAME_VERSION\s*=\s*"([^"]+)"/);
  return match ? match[1] : "unknown";
}
