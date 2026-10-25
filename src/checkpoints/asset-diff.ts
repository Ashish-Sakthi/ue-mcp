/**
 * Asset diffs for checkpoints: each version of a package loaded in the editor
 * with DiffUtils::LoadPackageForDiff, and Unreal's own diff code run on them
 * (the bridge's checkpoint_diff_asset). The records are the engine's, as it
 * produces them: FSingleObjectDiffEntry from DiffUtils::CompareUnrelatedObjects
 * with both values exported as text, FDiffSingleResult from
 * FGraphDiffControl::DiffGraphs with its DisplayString and ToolTip, and
 * FSCSDiffEntry from DiffUtils::CompareUnrelatedSCS. Facts about each version
 * (LOD counts, texture size) are context beside them, never a diff of their own.
 *
 * Each version also gets a picture rendered with ThumbnailTools::RenderThumbnail
 * on the object loaded for diff, kept per blob id, so a version always has the
 * same picture.
 */
import { unifiedDiff } from "./text-diff.js";

/** Bumped when the bridge's records change shape, so cached diffs are recomputed. */
export const ASSET_DIFF_FORMAT = 1;

/** How many packages one list_changes diffs up front; the rest are diffed on request, per path. */
export const MAX_ASSET_DIFFS = 25;

/** One record of Unreal's diff, as the engine produced it. */
export interface AssetDiffRecord {
  /** The engine's own message for it, such as "Base Color changed value in M_Blue" or "Added Node Constant". */
  displayString: string;
  /** EPropertyDiffType (PropertyValueChanged, PropertyAddedToA, PropertyAddedToB), EDiffType (NODE_ADDED, PIN_LINKEDTO_NODE...) or ETreeDiffType (NODE_ADDED...). */
  diffType: string;
  /** EDiffType::Category of a graph record: ADDITION, SUBTRACTION, MODIFICATION, MINOR. */
  category?: string;
  toolTip?: string;
  /** The actor, component, variable or row a record belongs to. */
  group?: string;
  /** FPropertySoftPath::ToDisplayName of a property record. */
  path?: string;
  property?: string;
  /** Both values exported with FProperty::ExportText; null where the path does not exist on that side. */
  before?: string | null;
  after?: string | null;
  graph?: string;
  node1?: { guid: string; name: string; class: string; title: string };
  node2?: { guid: string; name: string; class: string; title: string };
  pin1?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  pin2?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  /** For a NODE_PROPERTY graph record: the property records behind it. */
  properties?: AssetDiffRecord[];
  /** For an added or removed actor: its class, label and transform. */
  facts?: AssetFact[];
  treeLocation?: number[];
  owningObjectPath?: string;
  propertyDiffType?: string;
}

/** A group of records under one of the engine's own diff categories (Graph: EventGraph, Components, Defaults, Details...). */
export interface AssetDiffSection {
  category: string;
  kind: "properties" | "graph" | "components";
  /** The engine API the records came from. */
  engineSource: string;
  entries: AssetDiffRecord[];
  /** Differences under properties the details panel does not show, counted rather than listed. */
  hiddenChanges?: number;
}

export interface AssetFact {
  name: string;
  value: string;
}

export type AssetThumbnail =
  | { id: string; url: string; source: "rendered" | "saved"; note?: string }
  | { error: string };

/** One version of the package, as the editor loaded it. */
export interface AssetVersion {
  class: string;
  objectName?: string;
  facts: AssetFact[];
  thumbnail?: AssetThumbnail;
}

export interface AssetDiff {
  /** ready: diffed. deferred: over the up-front limit, diff it by path. error: see error. */
  state: "ready" | "deferred" | "error";
  error?: string;
  packageName?: string;
  assetClass?: string;
  status?: "added" | "modified" | "deleted";
  /** Which of Unreal's diffs serves this class, and what these records are. */
  engine?: { tool: string; note: string; native: "window" | "details" | "text"; nativeAvailable?: boolean; nativeReason?: string };
  /** null: the version did not exist. */
  before?: AssetVersion | null;
  after?: AssetVersion | null;
  classChanged?: string;
  sections?: AssetDiffSection[];
  /** The engine's own text form of the diff where its diff is a text diff (a DataTable's CSV), as a unified diff. */
  textDiff?: { format: string; diff: string };
  /** blueprint(diff)'s structural summary, for a Blueprint. */
  blueprint?: Record<string, unknown>;
  recordCount?: number;
  /** Records past maxRecords, counted. */
  recordsDropped?: number;
  /** One line to decide on. */
  summary: string;
  /** Whether Unreal's own diff view can open this, and why not. */
  nativeDiff: { available: boolean; reason?: string };
}

type Json = Record<string, unknown>;

const str = (v: unknown): string | undefined => (typeof v === "string" ? v : undefined);

function short(v: string | null | undefined, max = 60): string {
  if (v === null || v === undefined) return "none";
  const one = v.replace(/\s+/g, " ").trim();
  return one.length > max ? `${one.slice(0, max - 3)}...` : one;
}

function plural(n: number, one: string, many = `${one}s`): string {
  return `${n} ${n === 1 ? one : many}`;
}

/** The record's own words, with the values when it has them. */
export function describeRecord(r: AssetDiffRecord): string {
  const label = r.path ? (r.group ? `${r.group} ${r.path}` : r.path) : r.displayString;
  if (r.path && (r.before !== undefined || r.after !== undefined)) {
    if (r.diffType === "PropertyAddedToB") return `${label} added: ${short(r.after)}`;
    if (r.diffType === "PropertyAddedToA") return `${label} removed: ${short(r.before)}`;
    return `${label} ${short(r.before)} to ${short(r.after)}`;
  }
  return r.displayString;
}

/** A one-line account of a diff, from its records, for deciding Keep or Revert at a glance. */
export function summarizeAssetDiff(d: AssetDiff): string {
  if (d.state === "error") return `Could not diff: ${d.error ?? "no reason given"}`;
  if (d.state === "deferred") return "Not diffed yet: over the per-turn limit, diffed when opened.";
  const cls = d.assetClass ?? "asset";
  const sections = d.sections ?? [];
  const records = sections.flatMap((s) => s.entries);
  if (d.status === "added" || d.status === "deleted") {
    const v = d.status === "added" ? d.after : d.before;
    const facts = (v?.facts ?? []).filter((f) => f.value !== "").slice(0, 3).map((f) => `${f.name} ${short(f.value, 40)}`);
    const set = records.length ? `, ${plural(records.length, "property", "properties")} set` : "";
    return `${d.status === "added" ? "New" : "Deleted"} ${cls}${set}${facts.length ? `: ${facts.join("; ")}` : ""}`;
  }
  const hidden = sections.reduce((n, s) => n + (s.hiddenChanges ?? 0), 0);
  if (records.length === 0) {
    if (d.textDiff?.diff) return `${cls}: the engine's ${d.textDiff.format.toUpperCase()} export changed`;
    return hidden
      ? `${cls}: ${plural(hidden, "change")} only in properties the details panel does not show`
      : `${cls}: the file changed, and Unreal's diff finds no difference`;
  }
  const shown = records.slice(0, 2).map(describeRecord).join("; ");
  const total = records.length + (d.recordsDropped ?? 0);
  return `${plural(total, "change")}: ${shown}${total > 2 ? `; and ${total - 2} more` : ""}`;
}

/** Whether Unreal's own diff view can open this item, and why not, as the bridge decided it. */
export function nativeDiffOf(d: Pick<AssetDiff, "state" | "engine">): AssetDiff["nativeDiff"] {
  if (d.state !== "ready" || !d.engine) return { available: false, reason: "The asset has not been diffed." };
  return d.engine.nativeAvailable === false ? { available: false, reason: d.engine.nativeReason ?? "Unreal cannot open a diff of this type." } : { available: true };
}

/** The bridge's answer as an AssetDiff, with the CSV export turned into a unified diff and the pictures mapped to ids. */
export function assetDiffFromBridge(
  raw: Json,
  key: string,
  picture: (side: "before" | "after", thumb: Json | undefined) => AssetThumbnail | undefined,
): AssetDiff {
  const version = (side: "before" | "after"): AssetVersion | null => {
    const v = raw[side];
    if (!v || typeof v !== "object") return null;
    const o = v as Json;
    const thumbnail = picture(side, o.thumbnail as Json | undefined);
    return {
      class: str(o.class) ?? "",
      ...(str(o.objectName) ? { objectName: str(o.objectName) } : {}),
      facts: Array.isArray(o.facts) ? (o.facts as AssetFact[]) : [],
      ...(thumbnail ? { thumbnail } : {}),
    };
  };
  const d: AssetDiff = {
    state: "ready",
    packageName: str(raw.packageName),
    assetClass: str(raw.assetClass),
    status: str(raw.status) as AssetDiff["status"],
    engine: raw.engine as AssetDiff["engine"],
    before: version("before"),
    after: version("after"),
    sections: Array.isArray(raw.sections) ? (raw.sections as AssetDiffSection[]) : [],
    recordCount: typeof raw.recordCount === "number" ? raw.recordCount : 0,
    summary: "",
    nativeDiff: { available: false },
  };
  if (str(raw.classChanged)) d.classChanged = str(raw.classChanged);
  if (typeof raw.recordsDropped === "number") d.recordsDropped = raw.recordsDropped;
  if (raw.blueprint && typeof raw.blueprint === "object") d.blueprint = raw.blueprint as Json;
  const text = raw.engineText as Json | undefined;
  if (text && typeof text.before === "string" && typeof text.after === "string") {
    d.textDiff = { format: str(text.format) ?? "text", diff: unifiedDiff(text.before, text.after, key) ?? "The two exports differ in too many lines to show line by line." };
  }
  d.summary = summarizeAssetDiff(d);
  d.nativeDiff = nativeDiffOf(d);
  return d;
}

export function assetDiffError(error: string): AssetDiff {
  const d: AssetDiff = { state: "error", error, summary: "", nativeDiff: { available: false, reason: error } };
  d.summary = summarizeAssetDiff(d);
  return d;
}

export function assetDiffDeferred(): AssetDiff {
  const d: AssetDiff = { state: "deferred", summary: "", nativeDiff: { available: false, reason: "Diff it first." } };
  d.summary = summarizeAssetDiff(d);
  return d;
}
