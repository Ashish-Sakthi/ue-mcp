#!/usr/bin/env node
/**
 * Ask the tests/ue_mcp editor this checkout launched to close, and wait for it to exit.
 *
 *   node scripts/stop-editor.mjs
 *
 * For the release runner, which closes the smoke and automation editor before the live
 * suite sizes its shards to the memory left. The editor on this checkout's lockfile port
 * is asked which project it has open first, and only tests/ue_mcp is asked to close.
 * Unsaved test changes are discarded (requireClean: false).
 */
import { askOnce, extractReportedProjectDir, isProcessAlive, isTestProjectDir, PROJECT_IDENTITY_PYTHON, readPortLockfile } from "./bridge-target.mjs";

const lock = readPortLockfile();
if (!lock.pidAlive || !lock.port) {
  console.log("no tests/ue_mcp editor running");
  process.exit(0);
}
const url = `ws://127.0.0.1:${lock.port}`;
const reported = extractReportedProjectDir(await askOnce(url, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, 10000));
if (!isTestProjectDir(reported)) {
  console.error(`${url} has ${reported ?? "an unknown project"} open, not tests/ue_mcp; leaving it alone`);
  process.exit(1);
}
const reply = await askOnce(url, "request_editor_shutdown", { requireClean: false }, 15000);
if (reply?.success === false) {
  console.error(`the editor declined to close: ${reply.error ?? "no reason given"}`);
  process.exit(1);
}
for (let i = 0; i < 120 && isProcessAlive(lock.pid); i++) await new Promise((r) => setTimeout(r, 500));
const alive = isProcessAlive(lock.pid);
console.log(alive ? `editor ${lock.pid} did not exit within 60 s` : `editor ${lock.pid} closed`);
process.exit(alive ? 1 : 0);
