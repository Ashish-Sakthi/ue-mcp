import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { datasmithTool } from "../../src/tools/datasmith.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS as BRIDGE_TIMEOUTS } from "../../src/bridge/bridge-timeouts.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const buildCs = readFileSync(
  new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/UE_MCP_Bridge.Build.cs", import.meta.url),
  "utf8",
);

describe("datasmith category (#1383)", () => {
  const source = readHandlerFile("DatasmithHandlers.cpp");

  it("is a registered category with the five actions", () => {
    expect(ALL_TOOLS.map((t) => t.name)).toContain("datasmith");
    expect(Object.keys(datasmithTool.actions).sort()).toEqual(["import", "inspect", "query_actors", "reimport", "status"]);
    expect(datasmithTool.actions.import.description).toContain("Params: filePath, destinationPath, options?");
    expect(datasmithTool.actions.reimport.description).toContain("sceneAssetPath, filePath?, syncActors?, respawnDeleted?, options?");
    expect(datasmithTool.actions.query_actors.description).toContain("tagPrefix?, tags?, metadataKey?, metadataValue?");
  });

  it("classifies reads and mutations", () => {
    expect(classifyAction("datasmith.inspect", { filePath: "C:/a.udatasmith" }).mutates).toBe(false);
    expect(classifyAction("datasmith.query_actors", { tagPrefix: "Revit." }).mutates).toBe(false);
    expect(classifyAction("datasmith.import", { filePath: "C:/a.udatasmith", destinationPath: "/Game/DS" }).mutates).toBe(true);
    expect(classifyAction("datasmith.reimport", { sceneAssetPath: "/Game/DS/Scene" }).mutates).toBe(true);
  });

  it("gives the long calls the plugin's timeouts", () => {
    expect(BRIDGE_TIMEOUTS.datasmith_import).toBe(600);
    expect(BRIDGE_TIMEOUTS.datasmith_reimport).toBe(600);
    expect(datasmithTool.actions.import.timeoutMs).toBe(600_000);
  });

  it("reaches Datasmith by reflection only, and says when the plugin is off", () => {
    expect(buildCs).not.toMatch(/"Datasmith/);
    expect(source).not.toMatch(/#include\s+"Datasmith(?!Handlers)/);
    expect(source).toContain("Datasmith plugin not available");
    expect(source).toContain("bUpdateActors");
    expect(source).toContain("bRespawnDeletedActors");
    expect(source).toContain("GetDatasmithUserData");
  });
});

