// @live-editor headless
/**
 * The pie category's editor-time surface, read back after every write:
 * observation profiles and test scripts as assets, scenario validation, and
 * the multiplayer player and acceptance-run readers with no session running.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Pie/Assets";
const PROFILE = `${DIR}/OP_BCovAssets`;
const SCRIPT = `${DIR}/TS_BCovAssets`;
const CUBE = "BCov_Pie_AssetsCube";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json = {}): Promise<string> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should be refused: ${JSON.stringify(r.result)}`).toBe(false);
  return String(body(r).error ?? r.error ?? "");
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await call("place_actor", { actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE, location: { x: 300, y: -15000, z: 300 } });
}, 60_000);

afterAll(async () => {
  if (!bridge) return;
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  disconnectBridge();
});

describe("observation profiles", () => {
  it("profile_create, profile_read, profile_update, profile_list and profile_delete round-trip one asset", async () => {
    const created = await call("pie_profile_create", {
      name: "OP_BCovAssets", package_path: DIR,
      tracked_values: ["CustomTimeDilation", { path: "BaseEyeHeight", drift_threshold: 2 }],
      tracked_actors: ["SomeActor"], tracked_widgets: ["WBP_Hud"], position_threshold_cm: 7,
    });
    expect(created.created).toBe(true);
    expect(created.tracked_widgets).toEqual(["WBP_Hud"]);

    const read = await call("pie_profile_read", { path: PROFILE });
    expect((read.tracked_values as Json[]).map((v) => v.path)).toEqual(["CustomTimeDilation", "BaseEyeHeight"]);
    expect((read.tracked_values as Json[])[1].drift_threshold).toBe(2);
    expect(read.tracked_actors).toEqual(["SomeActor"]);
    expect(read.position_threshold_cm).toBe(7);

    const updated = await call("pie_profile_update", { path: PROFILE, tracked_actors: ["Other"], tracked_widgets: [], capture_montage: false });
    expect(updated.updated).toBe(true);
    const after = await call("pie_profile_read", { path: PROFILE });
    expect(after.tracked_actors).toEqual(["Other"]);
    expect(after.tracked_widgets).toEqual([]);
    expect(after.capture_montage).toBe(false);
    expect((after.tracked_values as Json[]).length).toBe(2);

    const listed = await call("pie_profile_list", { directory: DIR });
    expect((listed.profiles as Json[]).map((p) => p.name)).toContain("OP_BCovAssets");

    expect(await refused("pie_profile_delete", { path: PROFILE })).toMatch(/confirm/);
    const deleted = await call("pie_profile_delete", { path: PROFILE, confirm: true });
    expect(deleted.deleted).toBe(true);
    expect((await call("pie_profile_delete", { path: PROFILE, confirm: true })).alreadyDeleted).toBe(true);
    expect(((await call("pie_profile_list", { directory: DIR })).profiles as Json[]).length).toBe(0);
  }, 60_000);
});

describe("test scripts", () => {
  const steps = [
    { instruction: "Look down", judge: "auto", expect: { value: "BaseEyeHeight", op: "decreasing", within_ms: 3000 } },
    { instruction: "The door opens smoothly", judge: "manual" },
    { instruction: "The cube is in the level", expect: { value: CUBE, op: "exists" } },
  ];

  /** The structured refusal: every problem as { step, field, message, candidates? }. */
  async function refusedWith(method: string, params: Json): Promise<{ error: string; errors: Json[] }> {
    const r = await callBridge(bridge, method, params);
    expect(succeeded(r), `${method} should be refused: ${JSON.stringify(r.result)}`).toBe(false);
    expect(body(r).errorCode).toBe("INVALID_SCRIPT_STEPS");
    return { error: String(body(r).error ?? ""), errors: (body(r).errors as Json[]) ?? [] };
  }

  async function scriptListed(name: string): Promise<boolean> {
    return JSON.stringify((await call("pie_script_list", { directory: DIR })).scripts).includes(name);
  }

  it("script_create, script_read, script_update, script_list and script_delete round-trip one asset", async () => {
    const created = await call("pie_script_create", { name: "TS_BCovAssets", package_path: DIR, steps, description: "door check", default_within_ms: 4000 });
    expect(created.created).toBe(true);

    const read = await call("pie_script_read", { path: SCRIPT });
    const readSteps = read.steps as Json[];
    expect(readSteps.map((s) => s.judge)).toEqual(["auto", "manual", "auto"]);
    expect((readSteps[0].expect as Json).op).toBe("decreasing");
    expect((readSteps[0].expect as Json).within_ms).toBe(3000);
    expect(read.description).toBe("door check");
    expect(read.default_within_ms).toBe(4000);
    expect(read.manual_only).toBe(false);

    // Cut down to a manual step, the script checks nothing until it says so.
    const noAuto = await refusedWith("pie_script_update", { path: SCRIPT, steps: [steps[1]], description: "manual only" });
    expect(noAuto.errors).toEqual([expect.objectContaining({ step: null, field: "steps", candidates: ["manual_only"] })]);
    expect(((await call("pie_script_read", { path: SCRIPT })).steps as Json[]).length).toBe(3);

    const updated = await call("pie_script_update", { path: SCRIPT, steps: [steps[1]], description: "manual only", manual_only: true });
    expect(updated.updated).toBe(true);
    const after = await call("pie_script_read", { path: SCRIPT });
    expect((after.steps as Json[]).length).toBe(1);
    expect(after.description).toBe("manual only");
    expect(after.manual_only).toBe(true);

    // Clearing manual_only checks the stored steps again.
    expect((await refusedWith("pie_script_update", { path: SCRIPT, manual_only: false })).errors[0]).toMatchObject({ step: null, field: "steps" });
    expect((await call("pie_script_read", { path: SCRIPT })).manual_only).toBe(true);

    expect(await scriptListed("TS_BCovAssets")).toBe(true);

    const deleted = await call("pie_script_delete", { path: SCRIPT, confirm: true });
    expect(deleted.deleted ?? deleted.alreadyDeleted === false).toBe(true);
    expect(await scriptListed("TS_BCovAssets")).toBe(false);
  }, 60_000);

  it("script_create refuses a step whose expectation cannot be evaluated", async () => {
    const error = await refused("pie_script_create", {
      name: "TS_BCovBad", package_path: DIR, steps: [{ instruction: "x", expect: { value: "BaseEyeHeight", op: "eq" } }],
    });
    expect(error).toMatch(/target/i);
    const badOp = await refusedWith("pie_script_create", {
      name: "TS_BCovBad", package_path: DIR, steps: [{ instruction: "x", expect: { value: "BaseEyeHeight", op: "sideways" } }],
    });
    expect(badOp.errors[0]).toMatchObject({ step: 0, field: "expect.op" });
    expect(await scriptListed("TS_BCovBad")).toBe(false);
  }, 30_000);

  it("script_create refuses a value the observer cannot read, naming the step, the value and the closest candidates", async () => {
    const r = await refusedWith("pie_script_create", {
      name: "TS_BCovUnknown", package_path: DIR,
      steps: [
        { instruction: "Look", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } },
        { instruction: "Typo", expect: { value: "BaseEyeHieght", op: "gt", target: 0 } },
        { instruction: "Wrong actor", expect: { value: `${CUBE}x`, op: "exists" } },
        { instruction: "No widget", expect: { value: "WBP_BCovNoSuchWidget", op: "visible" } },
      ],
    });
    expect(r.errors.map((e) => [e.step, e.field])).toEqual([[1, "expect.value"], [2, "expect.value"], [3, "expect.value"]]);
    expect(String(r.errors[0].message)).toContain("BaseEyeHieght");
    expect(r.errors[0].candidates).toContain("BaseEyeHeight");
    expect(r.errors[1].candidates).toContain(CUBE);
    expect(String(r.errors[2].message)).toContain("resolve_in_pie");
    expect(r.error).toContain("BaseEyeHieght");
    expect(await scriptListed("TS_BCovUnknown")).toBe(false);
  }, 60_000);

  it("script_create refuses an op or target the value's type cannot meet", async () => {
    const r = await refusedWith("pie_script_create", {
      name: "TS_BCovTypes", package_path: DIR,
      steps: [
        { instruction: "A bool compared as a number", expect: { value: "bCanBeDamaged", op: "gt", target: 0 } },
        { instruction: "A float against true", expect: { value: "BaseEyeHeight", op: "gt", target: true } },
        { instruction: "exists against 2", expect: { value: CUBE, op: "exists", target: 2 } },
        { instruction: "A bool against true", expect: { value: "bCanBeDamaged", op: "eq", target: true } },
      ],
    });
    expect(r.errors.map((e) => [e.step, e.field])).toEqual([[0, "expect.op"], [1, "expect.target"], [2, "expect.target"]]);
    expect(r.errors[0].candidates).toEqual(["eq"]);
    expect(await scriptListed("TS_BCovTypes")).toBe(false);
  }, 30_000);

  it("resolve_in_pie lets a value through that only exists once PIE runs, and is stored on the step", async () => {
    const later = `${DIR}/TS_BCovLater`;
    const spawned = { instruction: "The spawned helper appears", expect: { value: "BP_BCovSpawnedLater", op: "exists" } };
    expect((await refusedWith("pie_script_create", { name: "TS_BCovLater", package_path: DIR, steps: [spawned] })).errors[0])
      .toMatchObject({ step: 0, field: "expect.value" });

    const created = await call("pie_script_create", { name: "TS_BCovLater", package_path: DIR, steps: [{ ...spawned, resolve_in_pie: true }] });
    expect(created.created).toBe(true);
    const read = await call("pie_script_read", { path: later });
    expect((read.steps as Json[])[0]).toMatchObject({ resolve_in_pie: true, expect: { value: "BP_BCovSpawnedLater", op: "exists" } });

    // An update naming an unreadable value is refused and leaves the stored steps alone.
    const bad = await refusedWith("pie_script_update", {
      path: later, steps: [{ instruction: "Typo", expect: { value: "BaseEyeHieght", op: "lt", target: 500 } }],
    });
    expect(bad.errors[0]).toMatchObject({ step: 0, field: "expect.value" });
    expect(((await call("pie_script_read", { path: later })).steps as Json[])[0].resolve_in_pie).toBe(true);
    await call("pie_script_delete", { path: later, confirm: true });
  }, 60_000);

  it("script_create refuses a script with no auto step unless it is manual_only", async () => {
    const manual = [{ instruction: "Does the menu feel right?", judge: "manual" }];
    const r = await refusedWith("pie_script_create", { name: "TS_BCovManual", package_path: DIR, steps: manual });
    expect(r.errors).toEqual([expect.objectContaining({ step: null, field: "steps" })]);
    expect(await scriptListed("TS_BCovManual")).toBe(false);

    const created = await call("pie_script_create", { name: "TS_BCovManual", package_path: DIR, steps: manual, manual_only: true });
    expect(created.created).toBe(true);
    expect((await call("pie_script_read", { path: `${DIR}/TS_BCovManual` })).manual_only).toBe(true);
    await call("pie_script_delete", { path: `${DIR}/TS_BCovManual`, confirm: true });
  }, 30_000);
});

describe("draft checks without saving", () => {
  it("script_validate answers the checks script_create runs, and saves nothing", async () => {
    const bad = await call("pie_script_validate", {
      steps: [
        { instruction: "Typo", expect: { value: "BaseEyeHieght", op: "gt", target: 0 } },
        { instruction: "A bool compared as a number", expect: { value: "bCanBeDamaged", op: "lt", target: 1 } },
      ],
    });
    expect(bad.valid).toBe(false);
    const errors = bad.errors as Json[];
    expect(errors.map((e) => [e.step, e.field])).toEqual([[0, "expect.value"], [1, "expect.op"]]);
    expect(errors[0].candidates).toContain("BaseEyeHeight");
    expect(String(bad.summary)).toContain("BaseEyeHieght");

    const good = await call("pie_script_validate", {
      steps: [{ instruction: "Look", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } }],
    });
    expect(good).toMatchObject({ valid: true, errors: [] });
    expect((good.steps as Json[])[0]).toMatchObject({ judge: "auto", expect: { value: "BaseEyeHeight", op: "gt", target: 0 } });

    const manual = await call("pie_script_validate", { steps: [{ instruction: "Feels right?", judge: "manual" }] });
    expect((manual.errors as Json[])[0]).toMatchObject({ step: null, field: "steps" });
    expect((await call("pie_script_validate", { steps: [{ instruction: "Feels right?", judge: "manual" }], manual_only: true })).valid).toBe(true);
    expect((await call("pie_script_list", { directory: DIR })).count).toBe(0);
  }, 30_000);

  it("script_values lists what a script can name, each of which script_validate accepts", async () => {
    const values = await call("pie_script_values", { target: "/Script/Engine.DefaultPawn", limit: 400 });
    const paths = (values.values as Json[]).map((v) => String(v.path));
    expect(paths).toContain("BaseEyeHeight");
    expect((values.values as Json[]).find((v) => v.path === "bCanBeDamaged")).toMatchObject({ kind: "boolean", type: "bool" });
    expect(values.actors as string[]).toContain(CUBE);
    expect((values.pawn as Json).class).toBeTruthy();
    expect(values.target).toMatchObject({ requested: "/Script/Engine.DefaultPawn", found: true });

    // A sample of the listed numbers reads back as valid steps.
    const numbers = (values.values as Json[]).filter((v) => v.kind === "number").slice(0, 10);
    expect(numbers.length).toBeGreaterThan(0);
    const check = await call("pie_script_validate", {
      steps: [
        ...numbers.map((v) => ({ instruction: `Watch ${v.path}`, expect: { value: v.path, op: "increasing" } })),
        { instruction: "The cube is there", expect: { value: CUBE, op: "exists" } },
      ],
    });
    expect(check.errors).toEqual([]);
    expect(await refused("pie_script_values", { profile: "/Game/MCPTest/NoSuchProfile" })).toMatch(/Profile not found/);
  }, 30_000);
});

describe("scenarios", () => {
  it("scenario_validate accepts a well-formed scenario and names each problem in a broken one", async () => {
    const good = await call("pie_scenario_validate", {
      scenario: { name: "ok", arrange: [], act: [], assert: [{ name: "no errors", channel: "$errors", op: "lte", value: 0, hold: "always" }] },
    });
    expect((good.validation as Json).valid).toBe(true);
    expect((good.validation as Json).assert_count).toBe(1);
    const bad = await callBridge(bridge, "pie_scenario_validate", {
      scenario: { name: "bad", arrange: [{ kind: "teleport_moon" }], act: [], assert: [{ name: "p", channel: "pos_z", op: "frobnicate" }] },
    });
    const v = body(bad).validation as Json;
    expect(v.valid).toBe(false);
    expect(JSON.stringify(v.errors)).toContain("teleport_moon");
    expect(JSON.stringify(v.errors)).toContain("frobnicate");
  }, 30_000);
});

describe("multiplayer readers with no session", () => {
  it("clients reports a session that is not networked", async () => {
    const clients = await call("pie_clients");
    expect(clients.networked).toBe(false);
    expect(clients.server_net_mode).toBe("none");
    expect(clients.clients).toEqual([]);
  });

  it("console points the editor's own player at execute_command and needs a remote player otherwise", async () => {
    expect(await refused("pie_console", { command: "stat fps", client: 0 })).toMatch(/execute_command/);
    expect(await refused("pie_console", { command: "stat fps", client: 1 })).toMatch(/remote players/i);
  });

  it("remote_result names a request that was never relayed", async () => {
    expect(await refused("pie_remote_result", { request_id: "inject-404" })).toMatch(/inject-404/);
  });

  it("uat_status reports no run in progress", async () => {
    const status = (await call("pie_uat_status")).status as Json;
    expect(["idle", "finished"]).toContain(status.state);
  });
});
