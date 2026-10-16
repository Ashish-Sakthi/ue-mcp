import { describe, expect, it, vi } from "vitest";
import { levelTool } from "../../src/tools/level.js";
import { handlerSpecs as levelSpecs } from "../../src/tools/specs/level.generated.js";
import { handlerSpecs as editorSpecs } from "../../src/tools/specs/editor.generated.js";
import type { ToolContext } from "../../src/core/types.js";

const paramNames = (specs: typeof levelSpecs, handler: string) => specs[handler].params.map((p) => p.name);

describe("level(load) unsaved-change guard (#1259)", () => {
  it("declares saveFirst, its saveCurrent alias, and discardUnsaved", () => {
    const params = Object.fromEntries(levelSpecs.load_level.params.map((p) => [p.name, p]));
    expect(params.saveFirst.type).toBe("boolean");
    expect(params.saveFirst.aliases).toContain("saveCurrent");
    expect(params.discardUnsaved.type).toBe("boolean");
  });

  it("forwards both flags to load_level as sent", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await levelTool.handler(ctx, { action: "load", levelPath: "/Game/Maps/A", saveFirst: true });
    expect(call).toHaveBeenCalledWith("load_level", { levelPath: "/Game/Maps/A", saveFirst: true }, undefined);
  });

  it("lets editor(open_asset) pass the same choice for a map", () => {
    expect(paramNames(editorSpecs, "open_asset")).toEqual(expect.arrayContaining(["saveFirst", "discardUnsaved"]));
  });
});
