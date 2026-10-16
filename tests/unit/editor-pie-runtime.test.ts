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
