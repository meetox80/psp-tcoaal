import { MemoryDirectory, WasiProcess } from "./wasi.js";

let compiled = null;

function buildArguments(hasVoices) {
  const args = ["tcoaal-builder", "/game", "-o", "/out/EBOOT.PBP"];
  if (hasVoices) args.push("--voices", "/voices");
  return args;
}

function makeFileSystem(gameFiles, voiceFiles) {
  const root = new MemoryDirectory();
  const game = root.makeDirectories(["game"]);
  const voices = root.makeDirectories(["voices"]);
  root.makeDirectories(["out"]);
  for (const file of gameFiles) game.addFile(file.path, file.blob);
  for (const file of voiceFiles) voices.addFile(file.path, file.blob);
  return root;
}

async function build({ wasmUrl, gameFiles, voiceFiles, env }) {
  compiled ??= WebAssembly.compileStreaming(fetch(wasmUrl));
  const module = await compiled;

  const root = makeFileSystem(gameFiles, voiceFiles);
  const process = new WasiProcess({
    args: buildArguments(voiceFiles.length > 0),
    env,
    root,
    onLine: (line) => self.postMessage({ type: "line", line }),
  });

  const started = performance.now();
  const code = process.run(module);
  const seconds = (performance.now() - started) / 1000;

  const output = root.find(["out", "EBOOT.PBP"]);
  if (code !== 0 || !output) {
    self.postMessage({ type: "done", code, seconds });
    return;
  }
  const eboot = output.contents();
  self.postMessage({ type: "done", code, seconds, eboot }, [eboot.buffer]);
}

self.onmessage = async (event) => {
  try {
    await build(event.data);
  } catch (error) {
    self.postMessage({ type: "crash", message: String(error?.stack ?? error) });
  }
};
