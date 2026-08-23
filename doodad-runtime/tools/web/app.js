const screen = document.querySelector("#screen");
const overlay = document.querySelector("#overlay");
const status = document.querySelector("#status");
const revision = document.querySelector("#revision");
const appName = document.querySelector("#app-name");
const message = document.querySelector("#message");
const engine = document.querySelector("#engine");
const renderer = document.querySelector("#renderer");
const packagePath = document.querySelector("#package");
const surfaceNote = document.querySelector("#surface-note");
const surfaceSize = document.querySelector("#surface-size");

let displayedRevision = -1;

async function refresh() {
  try {
    const response = await fetch("/state.json", { cache: "no-store" });
    const state = await response.json();
    status.textContent = state.status.toUpperCase();
    status.dataset.kind = state.status;
    revision.textContent = `revision ${state.revision}`;
    appName.textContent = state.app?.name ?? "Building package…";
    message.textContent = state.message;
    engine.textContent = state.engine;
    renderer.textContent = state.renderer;
    packagePath.textContent = state.package ?? "";
    if (state.surface) {
      const { width, height } = state.surface;
      document.documentElement.style.setProperty("--surface-aspect", `${width} / ${height}`);
      screen.width = width;
      screen.height = height;
      screen.alt = `${width} by ${height} Doodad preview`;
      surfaceNote.textContent = `${width} × ${height} T-Watch Ultra surface`;
      surfaceSize.textContent = `Portrait, ${width} × ${height}`;
    }

    if (state.revision !== displayedRevision && state.revision > 0) {
      displayedRevision = state.revision;
      screen.src = `/frame.bmp?revision=${state.revision}`;
    }
    overlay.textContent = state.stale ? `STALE PREVIEW\n${state.message}` : "";
    overlay.classList.toggle("hidden", !state.stale);
  } catch (error) {
    overlay.textContent = "DEV SERVER DISCONNECTED";
    overlay.classList.remove("hidden");
  }
}

refresh();
setInterval(refresh, 400);
