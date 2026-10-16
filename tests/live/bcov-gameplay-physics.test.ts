/**
 * Collision and physics body settings on a placed actor and on a Blueprint
 * component template, each read back from the component itself: the placed
 * actor's BodyInstance, and the template's effective collision.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Gameplay/Physics";
const BP = `${DIR}/BP_BCovCollision`;
const CUBE = "BCov_Gameplay_PhysCube";

type Json = Record<string, unknown>;
type Row = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Row[] => (Array.isArray(v) ? (v as Row[]) : []);

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** The cube's StaticMeshComponent BodyInstance, as Unreal export text. */
async function bodyInstance(): Promise<string> {
  const details = await call("get_component_details", { actorLabel: CUBE, includeValues: true, propertyNames: ["BodyInstance"] });
  const mesh = rows(details.components).find((c) => c.class === "StaticMeshComponent");
  return String((mesh?.values as Row | undefined)?.BodyInstance ?? "");
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: CUBE });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipe();
  await call("place_actor", {
    actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label: CUBE,
    location: { x: 0, y: -12000, z: 300 },
  });
  await call("set_actor_mobility", { actorLabel: CUBE, mobility: "movable" });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await wipe();
    disconnectBridge();
  }
});

describe("collision and physics on a placed actor", () => {
  // Fails live (#1223): skipped so its actions stay in the gap ratchet until it passes.
  it.skip("set_collision_profile and set_collision_enabled land on the component's BodyInstance", async () => {
    await call("set_collision_profile", { actorLabel: CUBE, profileName: "OverlapAll" });
    expect(await bodyInstance()).toMatch(/CollisionProfileName="?OverlapAll"?/);

    await call("set_collision_enabled", { actorLabel: CUBE, collisionEnabled: "QueryOnly" });
    expect(await bodyInstance()).toMatch(/CollisionEnabled=(ECollisionEnabled::)?QueryOnly\b/);
    await call("set_collision_enabled", { actorLabel: CUBE, collisionEnabled: "NoCollision" });
    expect(await bodyInstance()).toMatch(/CollisionEnabled=(ECollisionEnabled::)?NoCollision\b/);
  }, 120_000);

  // Fails live (#1223): skipped so its actions stay in the gap ratchet until it passes.
  it.skip("set_simulate_physics and set_physics_properties land on the component's BodyInstance", async () => {
    await call("set_collision_profile", { actorLabel: CUBE, profileName: "PhysicsActor" });
    await call("set_simulate_physics", { actorLabel: CUBE, simulate: true });
    expect(await bodyInstance()).toMatch(/bSimulatePhysics=True/);

    await call("set_physics_properties", { actorLabel: CUBE, mass: 42, linearDamping: 0.7, angularDamping: 0.3, enableGravity: false });
    const text = await bodyInstance();
    expect(text).toMatch(/MassInKgOverride=42(\.0+)?\b/);
    expect(text).toMatch(/LinearDamping=0\.70*\b/);
    expect(text).toMatch(/AngularDamping=0\.30*\b/);
    expect(text).toMatch(/bEnableGravity=False/);

    await call("set_simulate_physics", { actorLabel: CUBE, simulate: false });
    expect(await bodyInstance()).toMatch(/bSimulatePhysics=False/);
  }, 120_000);
});

describe("collision on a Blueprint component template", () => {
  it("set_collision writes the template get_component_collision reads", async () => {
    await call("create_blueprint", { name: "BP_BCovCollision", packagePath: DIR, parentClass: "Actor" });
    await call("add_component", { assetPath: BP, componentClass: "BoxComponent", componentName: "Box" });

    await call("set_collision", {
      assetPath: BP, componentName: "Box",
      collisionEnabled: "QueryOnly", objectType: "WorldDynamic", responseToAllChannels: "Overlap",
      responses: { Visibility: "Block" },
    });
    const read = await call("get_component_collision", { assetPath: BP, componentName: "Box" });
    expect(read.collisionEnabled).toBe("QueryOnly");
    expect(read.objectType).toBe("WorldDynamic");
    const response = (channel: string) => rows(read.responses).find((r) => r.channel === channel)?.response;
    expect(response("Visibility")).toBe("Block");
    expect(response("Camera")).toBe("Overlap");
  }, 120_000);
});
