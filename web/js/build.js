const PHASES = {
  database: { from: 0.03, to: 0.06, text: "Reading the game's data" },
  maps: { from: 0.06, to: 0.1, text: "Converting maps and events" },
  images: { from: 0.1, to: 0.8, text: "Scaling the artwork for the PSP screen" },
  audio: { from: 0.8, to: 0.84, text: "Copying music and sounds" },
  fonts: { from: 0.84, to: 0.9, text: "Rendering the font" },
  voices: { from: 0.9, to: 0.93, text: "Adding voice acting" },
  eboot: { from: 0.93, to: 0.99, text: "Writing EBOOT.PBP" },
};

export class Builder {
  constructor() {
    this.worker = null;
  }

  run({ gameFiles, voiceFiles, env, onProgress, onLine }) {
    this.worker ??= new Worker(new URL("./worker.js", import.meta.url), { type: "module" });

    return new Promise((resolve) => {
      const lines = [];
      let phase = PHASES.database;

      this.worker.onmessage = ({ data }) => {
        if (data.type === "line") {
          lines.push(data.line);
          onLine(data.line);

          const word = data.line.trim();
          if (PHASES[word]) {
            phase = PHASES[word];
            onProgress(phase.from, `${phase.text}...`);
          }

          const step = word.match(/^progress (\d+)\/(\d+)$/);
          if (step && phase === PHASES.images) {
            const done = Number(step[1]);
            const total = Number(step[2]);
            onProgress(phase.from + (phase.to - phase.from) * (done / total), `${phase.text}: ${done} of ${total}`);
          }
          return;
        }

        if (data.type === "crash") {
          resolve({ ok: false, crashed: true, message: data.message.split("\n")[0], lines });
          return;
        }

        const cracked = lines.map((line) => line.match(/looks cracked: (.*)$/)).find(Boolean);
        resolve({
          ok: data.code === 0 && Boolean(data.eboot),
          code: data.code,
          seconds: data.seconds,
          eboot: data.eboot ? new Uint8Array(data.eboot) : null,
          crackReason: data.code === 3 && cracked ? cracked[1] : null,
          lines,
        });
      };

      this.worker.postMessage({
        wasmUrl: new URL("../tcoaal-builder.wasm", import.meta.url).href,
        gameFiles,
        voiceFiles,
        env,
      });
    });
  }
}
