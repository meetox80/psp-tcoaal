import { unzipSync, zipSync } from "../vendor/fflate.js";
import { Builder } from "./build.js";
import { showCrackWarning } from "./crack-warning.js";
import { filesFromDrop, filesFromInput, filesFromZip, isZip, missingGameParts, readGameVersion, sortFiles } from "./files.js";
import { clearGame, loadGame, saveGame } from "./storage.js";

const SUPPORTED_VERSION = "3.0.12";

const ui = Object.fromEntries(
  [
    "drop", "pick-folder", "pick-zip", "game-empty", "game-ready", "game-summary", "game-error", "replace-game", "remove-game",
    "import-status", "import-bar", "import-text", "voices-option", "voices", "build", "build-status", "build-bar", "build-text",
    "log-box", "log", "build-error", "step-done", "download-zip", "download-eboot",
  ].map((id) => [id.replace(/-(\w)/g, (_, c) => c.toUpperCase()), document.getElementById(id)]),
);

const state = {
  game: null,
  eboot: null,
  building: false,
  voicesAvailable: false,
};

const builder = new Builder();

function showProgress(status, bar, label, fraction, text) {
  status.hidden = false;
  bar.style.width = `${Math.round(Math.min(1, Math.max(0, fraction)) * 100)}%`;
  label.textContent = text;
}

function showImportProgress(fraction, text) {
  showProgress(ui.importStatus, ui.importBar, ui.importText, fraction, text);
}

function showBuildProgress(fraction, text) {
  showProgress(ui.buildStatus, ui.buildBar, ui.buildText, fraction, text);
}

function setError(element, message) {
  element.hidden = !message;
  element.textContent = message ?? "";
}

function renderGame() {
  const game = state.game;
  ui.gameEmpty.hidden = Boolean(game);
  ui.gameReady.hidden = !game;
  ui.build.disabled = !game || state.building;
  if (!game) return;

  const where = game.stored ? "" : " (only for this visit; this browser can't keep it)";
  ui.gameSummary.textContent = `Game ready: version ${game.version}, ${game.files.length} files${where}`;
  setError(
    ui.gameError,
    game.version === SUPPORTED_VERSION ? "" : `This port is made for version ${SUPPORTED_VERSION} and your copy is ${game.version}, so the build will most likely refuse it.`,
  );
}

async function importGame(readFiles) {
  setError(ui.gameError, "");
  showImportProgress(0, "Reading your files...");
  try {
    const { stored, listed } = sortFiles(await readFiles());
    const missing = missingGameParts(stored.map((file) => file.path));
    if (missing.length > 0) throw new Error(`This doesn't look like the game folder. Missing: ${missing.join(", ")}.`);

    const version = await readGameVersion(stored);
    const megabytes = Math.round(stored.reduce((sum, file) => sum + file.blob.size, 0) / 1048576);
    const index = await saveGame(stored, listed, version, (fraction) =>
      showImportProgress(fraction, `Saving the game in this browser: ${Math.round(fraction * megabytes)} of ${megabytes} MB`),
    );

    state.game = { version, files: stored, listed, stored: Boolean(index) };
    ui.importStatus.hidden = true;
    renderGame();
  } catch (error) {
    ui.importStatus.hidden = true;
    setError(ui.gameError, error.message ?? String(error));
  }
}

async function loadVoiceFiles() {
  if (!state.voicesAvailable || !ui.voices.checked) return [];
  showBuildProgress(0.01, "Downloading voice acting...");
  const response = await fetch("voices.zip");
  if (!response.ok) return [];
  const archive = unzipSync(new Uint8Array(await response.arrayBuffer()));
  return Object.entries(archive)
    .filter(([path]) => !path.endsWith("/"))
    .map(([path, bytes]) => ({ path, blob: new Blob([bytes]) }));
}

function gameFilesForBuild() {
  const listed = state.game.listed.map((path) => ({ path, blob: null }));
  return [...state.game.files, ...listed];
}

async function runBuild(env) {
  ui.log.textContent = "";
  ui.logBox.hidden = false;
  setError(ui.buildError, "");
  showBuildProgress(0, "Getting ready...");

  return builder.run({
    gameFiles: gameFilesForBuild(),
    voiceFiles: await loadVoiceFiles(),
    env,
    onProgress: showBuildProgress,
    onLine: (line) => {
      ui.log.textContent += `${line}\n`;
    },
  });
}

function finishBuild(result) {
  if (result.ok) {
    state.eboot = result.eboot;
    const megabytes = (result.eboot.length / 1048576).toFixed(1);
    showBuildProgress(1, `Done in ${Math.round(result.seconds)} seconds: ${megabytes} MB`);
    ui.stepDone.hidden = false;
    ui.stepDone.scrollIntoView({ behavior: "smooth", block: "start" });
    return;
  }

  showBuildProgress(0, "Stopped.");
  if (result.crashed) {
    setError(ui.buildError, `The build crashed in this browser. Please try a current Chrome, Edge or Firefox. (${result.message})`);
    return;
  }
  const reason = result.lines.filter((line) => /error/i.test(line)).pop() ?? result.lines.at(-1) ?? "";
  setError(ui.buildError, `The build failed. ${reason}`);
}

async function startBuild() {
  state.building = true;
  ui.build.disabled = true;
  ui.stepDone.hidden = true;

  try {
    let result = await runBuild({});
    if (result.crackReason) {
      const accepted = await showCrackWarning(result.crackReason);
      if (!accepted) {
        showBuildProgress(0, "Stopped. Buy the game on Steam, then build again.");
        return;
      }
      result = await runBuild({ TCOAAL_CRACK_OK: "1" });
    }
    finishBuild(result);
  } catch (error) {
    showBuildProgress(0, "Stopped.");
    setError(ui.buildError, `Couldn't read the stored game files. Try choosing the game again. (${error.message ?? error})`);
  } finally {
    state.building = false;
    ui.build.disabled = !state.game;
  }
}

function download(blob, name) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

function downloadZip() {
  const zip = zipSync({ PSP: { GAME: { TCOAAL: { "EBOOT.PBP": [state.eboot, { level: 0 }] } } } });
  download(new Blob([zip], { type: "application/zip" }), "TCOAAL-PSP.zip");
}

function setUpDropZone() {
  const highlight = (on) => (event) => {
    event.preventDefault();
    ui.drop.classList.toggle("over", on);
  };
  ui.drop.addEventListener("dragenter", highlight(true));
  ui.drop.addEventListener("dragover", highlight(true));
  ui.drop.addEventListener("dragleave", highlight(false));
  ui.drop.addEventListener("drop", (event) => {
    highlight(false)(event);
    const items = [...event.dataTransfer.items];
    const single = event.dataTransfer.files.length === 1 ? event.dataTransfer.files[0] : null;
    if (single && isZip(single)) {
      importGame(() => filesFromZip(single, (fraction) => showImportProgress(fraction, `Unpacking ${single.name}: ${Math.round(fraction * 100)}%`)));
    } else {
      importGame(() => filesFromDrop(items, (count) => showImportProgress(0, `Reading folder: ${count} files found`)));
    }
  });
}

function setUpControls() {
  ui.pickFolder.addEventListener("change", (event) => {
    const files = filesFromInput(event.target.files);
    event.target.value = "";
    if (files.length > 0) importGame(async () => files);
  });

  ui.pickZip.addEventListener("change", (event) => {
    const zip = event.target.files[0];
    event.target.value = "";
    if (zip) importGame(() => filesFromZip(zip, (fraction) => showImportProgress(fraction, `Unpacking ${zip.name}: ${Math.round(fraction * 100)}%`)));
  });

  ui.replaceGame.addEventListener("click", () => {
    ui.gameEmpty.hidden = false;
    ui.gameReady.hidden = true;
  });

  ui.removeGame.addEventListener("click", async () => {
    if (!confirm("Remove the stored game files from this browser? Your PSP saves are not affected.")) return;
    await clearGame();
    state.game = null;
    renderGame();
  });

  ui.build.addEventListener("click", startBuild);
  ui.downloadEboot.addEventListener("click", () => download(new Blob([state.eboot]), "EBOOT.PBP"));
  ui.downloadZip.addEventListener("click", downloadZip);

  window.addEventListener("beforeunload", (event) => {
    if (state.building) event.preventDefault();
  });
}

async function detectVoices() {
  try {
    const response = await fetch("voices.zip", { method: "HEAD" });
    return response.ok;
  } catch {
    return false;
  }
}

async function restoreGame() {
  const saved = await loadGame();
  if (!saved) return null;
  return { version: saved.index.version, files: saved.files, listed: saved.index.listed, stored: true };
}

async function start() {
  setUpDropZone();
  setUpControls();
  state.voicesAvailable = await detectVoices();
  ui.voicesOption.hidden = !state.voicesAvailable;
  state.game = await restoreGame();
  renderGame();
}

start();
