#!/usr/bin/env node
/**
 * The release gates, run and timed as one (#1465).
 *
 *   node scripts/release-run.mjs [--binaries] [--log-dir DIR] [--record-baseline]
 *
 * The editor lane deploys, builds, runs the sharded live suite, then launches
 * tests/ue_mcp for smoke, automation and the lifecycle cases. Once the live
 * suite is done, the editor-free lane (the release gates and the unit,
 * multi-editor and integration suites) and, with --binaries, the per-engine
 * binary builds run beside the rest of the editor lane.
 *
 * Every step is run, none is skipped on another's failure, and each one's
 * output goes to its own log. The timing goes to release-timing.json in the
 * log directory; --record-baseline also writes it to
 * tests/timing/release-baseline.json.
 */
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const flag = (name) => {
  const i = argv.indexOf(name);
  return i === -1 ? null : argv[i + 1];
};
const logDir = path.resolve(flag("--log-dir") ?? path.join(ROOT, "release-logs"));
const BASELINE = path.join(ROOT, "tests", "timing", "release-baseline.json");

const npm = process.platform === "win32" ? "npm.cmd" : "npm";
const node = process.execPath;

/** Each step: a name and the command it runs. */
const EDITOR_LANE = [
  ["deploy", node, ["scripts/deploy.mjs"]],
  ["build", npm, ["run", "build"]],
  ["live", npm, ["run", "test:live"]],
  ["up", npm, ["run", "up"]],
  ["smoke", npm, ["run", "test:smoke"]],
  ["automation", npm, ["run", "test:automation"]],
  ["lifecycle", npm, ["run", "test:live", "--", "--lifecycle"]],
];
const FREE_LANE = [
  ["release-gates", node, ["scripts/check-release-gates.mjs", "all"]],
  ["unit", npm, ["test"]],
];
const BINARIES = ["binaries", npm, ["run", "binaries:build"]];

const steps = [];
const t0 = Date.now();
const stamp = (ms) => `${((ms - t0) / 1000).toFixed(0).padStart(5)}s`;

function run([name, cmd, args]) {
  fs.mkdirSync(logDir, { recursive: true });
  const logFile = path.join(logDir, `${name}.log`);
  const out = fs.openSync(logFile, "w");
  const started = Date.now();
  console.log(`${stamp(started)} start  ${name}`);
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { cwd: ROOT, stdio: ["ignore", out, out], shell: process.platform === "win32" });
    child.on("close", (code) => {
      fs.closeSync(out);
      const ended = Date.now();
      const step = { name, code, startS: (started - t0) / 1000, durationS: (ended - started) / 1000, log: logFile };
      steps.push(step);
      console.log(`${stamp(ended)} ${code === 0 ? "pass " : "FAIL "} ${name} (${step.durationS.toFixed(0)}s)`);
      resolve(step);
    });
  });
}

async function lane(list, afterEach) {
  for (const s of list) {
    await run(s);
    await afterEach?.(s[0]);
  }
}

// The live suite sizes its shards to the cores and memory free, so nothing else
// runs beside it: on the reference machine the unit suite alongside it took the
// live run from 301 s to 505 s. Everything editor-free starts when it is done.
const after = [];
await lane(EDITOR_LANE, (name) => {
  if (name !== "live") return;
  after.push(lane(FREE_LANE));
  if (argv.includes("--binaries")) after.push(run(BINARIES));
});
await Promise.all(after);

const wallS = (Date.now() - t0) / 1000;
const record = {
  recordedAt: new Date().toISOString(),
  machine: { cpus: os.cpus().length, memoryGB: Math.round(os.totalmem() / 1073741824) },
  wallS,
  steps: steps.sort((a, b) => a.startS - b.startS).map(({ log, ...s }) => s),
};
fs.writeFileSync(path.join(logDir, "release-timing.json"), `${JSON.stringify(record, null, 2)}\n`);
if (argv.includes("--record-baseline")) {
  fs.mkdirSync(path.dirname(BASELINE), { recursive: true });
  fs.writeFileSync(BASELINE, `${JSON.stringify(record, null, 2)}\n`);
}

const slowestStep = [...record.steps].sort((a, b) => b.durationS - a.durationS)[0];
console.log(`\nwall clock ${wallS.toFixed(0)}s; slowest step ${slowestStep?.name} (${slowestStep?.durationS.toFixed(0)}s)`);
const failed = record.steps.filter((s) => s.code !== 0);
if (failed.length) {
  console.log(`failed: ${failed.map((s) => `${s.name} (see ${path.join(logDir, `${s.name}.log`)})`).join(", ")}`);
  process.exit(1);
}
