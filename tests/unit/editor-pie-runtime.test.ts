import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import type { ToolContext } from "../../src/core/types.js";

/**
 * Core PIE runtime actions. Each case asserts that the parameters a caller
 * passes reach the bridge method under the names the C++ handler reads.
 */
function mockContext() {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  return { call, ctx };
}

function forwarded(call: ReturnType<typeof vi.fn>, method: string): Record<string, unknown> {
  const hit = call.mock.calls.find((c) => c[0] === method);
  expect(hit, `bridge method ${method} was called`).toBeDefined();
  return hit![1] as Record<string, unknown>;
}

describe("PIE pause (#1215)", () => {
  it("forwards paused, mode and force to set_pie_paused", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, { action: "set_pie_paused", paused: false, mode: "game", force: true, pieInstance: 1 });
    const bag = forwarded(call, "set_pie_paused");
    expect(bag.paused).toBe(false);
    expect(bag.mode).toBe("game");
    expect(bag.force).toBe(true);
    expect(bag.pieInstance).toBe(1);
  });
});

describe("runtime target propertyPath (#1215)", () => {
  it.each(["invoke_object_function", "get_object_properties", "set_object_property"])(
    "%s forwards propertyPath",
    async (action) => {
      const { call, ctx } = mockContext();
      await editorTool.handler(ctx, {
        action,
        target: "gameinstance",
        propertyPath: "GameSettings.Game Settings",
        functionName: "SetSomeOption",
        propertyName: "bEnabled",
        value: true,
      });
      expect(forwarded(call, action).propertyPath).toBe("GameSettings.Game Settings");
    },
  );

  it("is accepted on each invoke_object_functions entry", () => {
    const parsed = editorTool.schema.calls.safeParse([
      { functionName: "SetSomeOption", target: "gameinstance", propertyPath: "GameSettings.Game Settings" },
    ]);
    expect(parsed.success).toBe(true);
  });
});

describe("runtime property paths and physics reads (#1212)", () => {
  it("get_object_properties forwards dotted and indexed paths untouched", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, {
      action: "get_object_properties",
      target: "gamemode",
      propertyNames: ["StashWorld.KickRadiusCm", "Items[0].Count", "bIsActive"],
    });
    expect(forwarded(call, "get_object_properties").propertyNames).toEqual([
      "StashWorld.KickRadiusCm",
      "Items[0].Count",
      "bIsActive",
    ]);
  });

  it("query_components forwards the physics field group against the PIE world", async () => {
    const { levelTool } = await import("../../src/tools/level.js");
    const { call, ctx } = mockContext();
    await levelTool.handler(ctx, { action: "query_components", actorClass: "StaticMeshActor", fields: ["physics"], world: "pie" });
    const bag = forwarded(call, "query_components");
    expect(bag.fields).toEqual(["physics"]);
    expect(bag.world).toBe("pie");
  });
});

describe("configure_pie client windows (#1235)", () => {
  it("forwards client window size, position and centering", async () => {
    const { call, ctx } = mockContext();
    await editorTool.handler(ctx, {
      action: "configure_pie",
      clientWindowWidth: 960,
      clientWindowHeight: 540,
      newWindowPosition: { x: 1920, y: 0 },
      centerNewWindow: false,
    });
    const bag = forwarded(call, "configure_pie");
    expect(bag.clientWindowWidth).toBe(960);
    expect(bag.clientWindowHeight).toBe(540);
    expect(bag.newWindowPosition).toEqual({ x: 1920, y: 0 });
    expect(bag.centerNewWindow).toBe(false);
  });

  it("refuses a position without both coordinates", () => {
    expect(editorTool.schema.newWindowPosition.safeParse({ x: 10 }).success).toBe(false);
  });
});
