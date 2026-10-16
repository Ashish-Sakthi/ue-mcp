// The behavioural-coverage files and the one rule the ratchet adds on top of
// the inventory: the grandfathered gap list may only shrink (#1223).
// Shared by the generator, its unit test and the release gate.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/** Repo-relative paths, so a gate can read the same file at another commit. */
export const COVERAGE_DIR = "tests/coverage";
export const INVENTORY_FILE = `${COVERAGE_DIR}/behavioural-coverage.json`;
export const RATCHET_FILE = `${COVERAGE_DIR}/behavioural-gaps.json`;
export const EXEMPTIONS_FILE = `${COVERAGE_DIR}/behavioural-exemptions.json`;

/** An exemption reason shorter than this is a label, not a reason. */
export const MIN_EXEMPTION_REASON = 40;

/**
 * Parse the ratchet file's text: `{ "note": string, "gaps": string[] }`.
 * @param {string} text
 * @returns {string[]}
 */
export function parseRatchet(text) {
  const parsed = JSON.parse(text);
  if (!parsed || !Array.isArray(parsed.gaps)) throw new Error(`${RATCHET_FILE} has no "gaps" array`);
  return parsed.gaps.map(String);
}

/** @returns {string[]} */
export function readRatchet(root = REPO_ROOT) {
  return parseRatchet(fs.readFileSync(path.join(root, RATCHET_FILE), "utf8"));
}

/**
 * Parse the exemptions file's text: `{ "note": string, "exemptions": { method: reason } }`.
 * @param {string} text
 * @returns {Record<string, string>}
 */
export function parseExemptions(text) {
  const parsed = JSON.parse(text);
  if (!parsed || typeof parsed.exemptions !== "object" || parsed.exemptions === null) {
    throw new Error(`${EXEMPTIONS_FILE} has no "exemptions" object`);
  }
  return Object.fromEntries(Object.entries(parsed.exemptions).map(([k, v]) => [k, String(v)]));
}

/** @returns {Record<string, string>} */
export function readExemptions(root = REPO_ROOT) {
  return parseExemptions(fs.readFileSync(path.join(root, EXEMPTIONS_FILE), "utf8"));
}

/**
 * Entries the head ratchet carries that the baseline did not. Any entry is a
 * newly grandfathered gap, which is exactly what the ratchet exists to refuse.
 * @param {string[]} baseline
 * @param {string[]} head
 * @returns {string[]}
 */
export function ratchetGrowth(baseline, head) {
  const before = new Set(baseline);
  return head.filter((m) => !before.has(m)).sort();
}
