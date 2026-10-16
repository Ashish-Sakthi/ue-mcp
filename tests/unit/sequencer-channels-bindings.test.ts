import { describe, expect, it } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const channels = readHandlerFile("SequencerHandlers_Channels.cpp");
const bindings = readHandlerFile("SequencerHandlers_Bindings.cpp");
const sequencer = readHandlerFile("SequencerHandlers.cpp");

function body(source: string, fn: string, next?: string): string {
  const start = source.indexOf(`FSequencerHandlers::${fn}(`);
  expect(start).toBeGreaterThan(-1);
  const end = next ? source.indexOf(`FSequencerHandlers::${next}(`, start) : source.length;
  return source.slice(start, end > start ? end : source.length);
}

describe("sequencer channel actions (#1407)", () => {
  it("advertises the channel actions with their C++ parameters", () => {
    const read = editorTool.actions.get_sequence_channel_keys;
    expect(read.description).toContain("Params: sequencePath (or assetPath, or path), trackType?, trackName?, trackIndex?");
    expect(read.description).toContain("sampleStartFrame?");
    const write = editorTool.actions.set_sequence_channel_keys;
    expect(write.description).toContain("channel, keyframes?, interpolation?, clearExisting?, defaultValue?, clearDefault?, parameterType?");
    expect(editorTool.schema.parameterType.safeParse("color").success).toBe(true);
    expect(editorTool.schema.materialParameterCollection.safeParse("/Game/MPC_Ocean").success).toBe(true);
  });

  it("classifies the reader as a read and the writer as a mutation", () => {
    expect(classifyAction("editor.get_sequence_channel_keys", { sequencePath: "/Game/S", trackType: "MPC" }).mutates).toBe(false);
    expect(classifyAction("editor.set_sequence_channel_keys", { sequencePath: "/Game/S", trackType: "MPC", channel: "Intensity" }).mutates).toBe(true);
    expect(classifyAction("editor.remove_sequence_section", { sequencePath: "/Game/S", trackType: "Transform", sectionIndex: 1 }).mutates).toBe(true);
  });

  it("reads keys without writing to the section", () => {
    const get = body(channels, "GetChannelKeys", "SetChannelKeys");
    for (const write of ["Modify()", "MarkPackageDirty", "WriteKey(", "ClearKeys("]) expect(get).not.toContain(write);
    expect(get).toContain("EvaluateChannel(");
  });

  it("validates every key before it touches the section, and reads the channel back", () => {
    const set = body(channels, "SetChannelKeys", "RemoveSection");
    const firstWrite = set.indexOf("R.Section->Modify()");
    for (const guard of ["needs a time", "must be a number or a boolean", "is not cubic, linear or constant"]) {
      expect(set.indexOf(guard)).toBeGreaterThan(-1);
      expect(set.indexOf(guard)).toBeLessThan(firstWrite);
    }
    expect(set).toContain("keysVerified");
    expect(set).toContain('MCPSetRollback(Result, TEXT("set_sequence_channel_keys")');
  });

  it("names parameter-section channels by parameter, so MPC curves are addressable", () => {
    const header = readHandlerFile("HandlerSequencerTracks.h");
    for (const name of ["GetScalarParameterNamesAndCurves", "GetColorParameterNamesAndCurves", 'TEXT(".R")', "MaterialParameterCollection"]) {
      expect(header).toContain(name);
    }
  });

  it("keys with constant interpolation in set_sequence_keyframes (#1232)", () => {
    const keys = body(sequencer, "SetKeyframes");
    expect(keys).toContain("AddConstantKey");
    expect(keys).toContain("Unknown interpolation");
  });

  it("removes a section inside a transaction whose undo is the rollback (#1232)", () => {
    const remove = body(channels, "RemoveSection");
    expect(remove).toContain("FScopedTransaction");
    expect(remove).toContain('MCPSetRollback(Result, TEXT("undo_redo_steps")');
    expect(remove).toContain("Pass sectionIndex");
  });
});

describe("sequencer runtime bindings (#1232)", () => {
  it("advertises runtime bindings as a read", () => {
    expect(classifyAction("editor.get_sequence_runtime_bindings", {}).mutates).toBe(false);
  });

  it("asks each runtime player, not the editor, what a binding resolved to", () => {
    const runtime = body(bindings, "GetRuntimeBindings");
    expect(runtime).toContain("Player->GetBoundObjects(");
    expect(runtime).toContain("TransformOrigin");
  });
});
