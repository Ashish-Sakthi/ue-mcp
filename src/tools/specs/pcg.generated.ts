// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd pcg handler. */
export const handlerSpecs: HandlerSpecs = {
  "add_pcg_node": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeType",
        "type": "string",
        "required": true,
        "description": "PCG settings class of the node to add"
      },
      {
        "name": "posX",
        "type": "number",
        "required": false,
        "description": "Graph editor X position for the new node"
      },
      {
        "name": "posY",
        "type": "number",
        "required": false,
        "description": "Graph editor Y position for the new node"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Engine name for the node (default: engine-assigned). An existing node of the same class with this name is reported as existed; another class is refused"
      }
    ]
  },
  "add_pcg_volume": {
    "category": "pcg",
    "params": [
      {
        "name": "graphPath",
        "type": "string",
        "required": false,
        "description": "PCGGraph to assign to the volume's component"
      },
      {
        "name": "location",
        "type": "vec3",
        "required": false,
        "description": "World location {x,y,z} (default origin); wins over transform.location"
      },
      {
        "name": "transform",
        "type": "object",
        "required": false,
        "description": "Actor transform; scale multiplies the extent",
        "fields": [
          {
            "name": "location",
            "type": "vec3",
            "required": false,
            "description": "World location"
          },
          {
            "name": "rotation",
            "type": "rotator",
            "required": false,
            "description": "World rotation"
          },
          {
            "name": "scale",
            "type": "vec3",
            "required": false,
            "description": "Actor scale (default 1)"
          },
          {
            "name": "scale3D",
            "type": "vec3",
            "required": false,
            "description": "Same as scale"
          }
        ]
      },
      {
        "name": "extent",
        "type": "vec3",
        "required": false,
        "description": "Half-size of the volume box {x,y,z} before scale (default 500 on each axis)"
      },
      {
        "name": "label",
        "type": "string",
        "required": false,
        "description": "Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the label exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Spawns a volume under the contract values; nothing it reads fails first"
  },
  "cleanup_pcg": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "removeComponents",
        "type": "boolean",
        "required": false,
        "description": "Remove the managed spawned components too (default true)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "connect_pcg_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": true,
        "description": "Node the edge leaves",
        "aliases": [
          "sourceNodeName"
        ]
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Output pin label. connect_nodes defaults to the first output pin, disconnect_nodes to any",
        "aliases": [
          "sourcePinLabel"
        ]
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": true,
        "description": "Node the edge enters",
        "aliases": [
          "targetNodeName"
        ]
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Input pin label. connect_nodes defaults to the first input pin, disconnect_nodes to any",
        "aliases": [
          "targetPinLabel"
        ]
      }
    ]
  },
  "create_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Graph asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new graph (default /Game/PCG)",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the graph exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a graph under the contract values; nothing it reads fails first"
  },
  "disconnect_pcg_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "sourceNode",
        "type": "string",
        "required": true,
        "description": "Node the edge leaves",
        "aliases": [
          "sourceNodeName"
        ]
      },
      {
        "name": "targetNode",
        "type": "string",
        "required": true,
        "description": "Node the edge enters",
        "aliases": [
          "targetNodeName"
        ]
      },
      {
        "name": "sourcePin",
        "type": "string",
        "required": false,
        "description": "Output pin label. connect_nodes defaults to the first output pin, disconnect_nodes to any",
        "aliases": [
          "sourcePinLabel"
        ]
      },
      {
        "name": "targetPin",
        "type": "string",
        "required": false,
        "description": "Input pin label. connect_nodes defaults to the first input pin, disconnect_nodes to any",
        "aliases": [
          "targetPinLabel"
        ]
      }
    ]
  },
  "execute_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "seed",
        "type": "integer",
        "required": false,
        "description": "Write the component's Seed before generating; the old one is reported as previousSeed"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "export_level_to_pcg_asset": {
    "category": "pcg",
    "params": [
      {
        "name": "levelPath",
        "type": "string",
        "required": true,
        "description": "Saved level (.umap) to export, package or object path"
      },
      {
        "name": "assetPath",
        "type": "string",
        "required": false,
        "description": "The PCG data asset's own path (an object path, or an existing asset's package path, updated in place), or else the folder for it (default: the level's folder)"
      },
      {
        "name": "assetName",
        "type": "string",
        "required": false,
        "description": "Asset name (default: <LevelName>_PCG)"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the asset after export (default true)"
      }
    ]
  },
  "export_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "includeSettings",
        "type": "boolean",
        "required": false,
        "description": "Include per-node editable settings in the response (default true)"
      }
    ]
  },
  "force_regenerate_pcg": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "get_pcg_component_details": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "get_pcg_components": {
    "category": "pcg",
    "params": []
  },
  "import_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodes",
        "type": "array",
        "required": true,
        "description": "[{name, class, posX?, posY?, settings?}]",
        "items": "object"
      },
      {
        "name": "connections",
        "type": "array",
        "required": false,
        "description": "[{from, fromPin?, to, toPin?}]",
        "items": "object"
      },
      {
        "name": "replace",
        "type": "boolean",
        "required": false,
        "description": "Wipe existing user nodes first (default false)"
      },
      {
        "name": "removeNodes",
        "type": "array",
        "required": false,
        "description": "Without replace: node names to remove before importing; a missing name is skipped",
        "items": "string"
      },
      {
        "name": "removeConnections",
        "type": "array",
        "required": false,
        "description": "Without replace: edges [{from, fromPin, to, toPin}] to cut before importing",
        "items": "object"
      }
    ]
  },
  "list_pcg_graphs": {
    "category": "pcg",
    "params": [
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 2000)"
      }
    ]
  },
  "read_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      }
    ]
  },
  "read_pcg_node_settings": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      }
    ]
  },
  "remove_pcg_node": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      }
    ]
  },
  "set_pcg_graph_pins": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "side",
        "type": "string",
        "required": true,
        "description": "input | output: the graph node whose pins change"
      },
      {
        "name": "pins",
        "type": "array",
        "required": true,
        "description": "Pins matched by label",
        "items": "object",
        "fields": [
          {
            "name": "label",
            "type": "string",
            "required": true,
            "description": "Pin label; edges are wired by it"
          },
          {
            "name": "type",
            "type": "string",
            "required": false,
            "description": "EPCGDataType name: Any (default for a new pin), Point, Spatial, Param, PolyLine, Surface...; omitted keeps an existing pin's type"
          },
          {
            "name": "tooltip",
            "type": "string",
            "required": false,
            "description": "Pin tooltip"
          },
          {
            "name": "allowMultipleData",
            "type": "boolean",
            "required": false,
            "description": "Accept more than one data (default true)"
          },
          {
            "name": "allowMultipleConnections",
            "type": "boolean",
            "required": false,
            "description": "Accept more than one edge (default true)"
          }
        ]
      },
      {
        "name": "mode",
        "type": "string",
        "required": false,
        "description": "append (default: add, or update a pin with the same label) | replace (the pins become exactly this list) | remove (labels only)"
      }
    ]
  },
  "set_pcg_node_settings": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      },
      {
        "name": "settings",
        "type": "object",
        "required": false,
        "description": "{propertyPath: value}; dotted paths and nested structs supported"
      },
      {
        "name": "propertyName",
        "type": "string",
        "required": false,
        "description": "One property to write instead of a settings object"
      },
      {
        "name": "propertyValue",
        "type": "string",
        "required": false,
        "description": "The value for propertyName, as UE export text"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "settings"
          ],
          [
            "propertyName",
            "propertyValue"
          ]
        ]
      }
    ]
  },
  "set_pcg_subgraph": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the Subgraph node, as read_graph reports it"
      },
      {
        "name": "subgraphPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph or PCGGraphInstance to run; \"\" clears it"
      }
    ]
  },
  "set_pcg_subgraph_parameters": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the Subgraph node"
      },
      {
        "name": "parameters",
        "type": "object",
        "required": true,
        "description": "{parameterName: value}; null clears that override"
      }
    ]
  },
  "set_static_mesh_spawner_meshes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": true,
        "description": "Engine name of the node, as read_graph reports it"
      },
      {
        "name": "entries",
        "type": "array",
        "required": true,
        "description": "Weighted mesh entries",
        "items": "object",
        "fields": [
          {
            "name": "mesh",
            "type": "string",
            "required": true,
            "description": "StaticMesh, package or object path; an entry without one is skipped, and one that does not load refuses the call"
          },
          {
            "name": "weight",
            "type": "number",
            "required": false,
            "description": "Relative pick weight (default 1), truncated to a whole number"
          }
        ]
      },
      {
        "name": "replace",
        "type": "boolean",
        "required": false,
        "description": "Overwrite existing MeshEntries (default true)"
      }
    ]
  },
  "toggle_pcg_graph": {
    "category": "pcg",
    "params": [
      {
        "name": "actorLabel",
        "type": "string",
        "required": false,
        "description": "Editor label of the actor holding the PCG component; a label naming several actors is refused"
      },
      {
        "name": "actorPath",
        "type": "string",
        "required": false,
        "description": "Full actor object path; the unambiguous selector"
      },
      {
        "name": "graphPath",
        "type": "string",
        "required": false,
        "description": "PCGGraph to assign (default: re-apply the component's current graph)"
      }
    ],
    "choices": [
      {
        "mode": "exactlyOne",
        "branches": [
          [
            "actorLabel"
          ],
          [
            "actorPath"
          ]
        ]
      }
    ]
  },
  "unwrap_pcg_instance_nodes": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "PCGGraph asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "nodeName",
        "type": "string",
        "required": false,
        "description": "Only this node (default: every node in the graph)"
      }
    ]
  },
  "update_pcg_level_assets": {
    "category": "pcg",
    "params": [
      {
        "name": "assetPaths",
        "type": "array",
        "required": true,
        "description": "PCG data assets to re-export from their source levels",
        "items": "string"
      },
      {
        "name": "save",
        "type": "boolean",
        "required": false,
        "description": "Save the assets after export (default true)"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  add_pcg_node: "Params: assetPath (or path), nodeType, posX?, posY?, name?",
  add_pcg_volume: "Params: graphPath?, location?, transform?, extent?, label?, onConflict?",
  cleanup_pcg: "Params: actorLabel OR actorPath, removeComponents?",
  connect_pcg_nodes: "Params: assetPath (or path), sourceNode (or sourceNodeName), sourcePin? (or sourcePinLabel), targetNode (or targetNodeName), targetPin? (or targetPinLabel)",
  create_pcg_graph: "Params: name, packagePath? (or path), onConflict?",
  disconnect_pcg_nodes: "Params: assetPath (or path), sourceNode (or sourceNodeName), targetNode (or targetNodeName), sourcePin? (or sourcePinLabel), targetPin? (or targetPinLabel)",
  execute_pcg_graph: "Params: actorLabel OR actorPath, seed?",
  export_level_to_pcg_asset: "Params: levelPath, assetPath?, assetName?, save?",
  export_pcg_graph: "Params: assetPath (or path), includeSettings?",
  force_regenerate_pcg: "Params: actorLabel OR actorPath",
  get_pcg_component_details: "Params: actorLabel OR actorPath",
  get_pcg_components: "Params: none",
  import_pcg_graph: "Params: assetPath (or path), nodes, connections?, replace?, removeNodes?, removeConnections?",
  list_pcg_graphs: "Params: cursor?, limit?",
  read_pcg_graph: "Params: assetPath (or path)",
  read_pcg_node_settings: "Params: assetPath (or path), nodeName",
  remove_pcg_node: "Params: assetPath (or path), nodeName",
  set_pcg_graph_pins: "Params: assetPath (or path), side, pins, mode?",
  set_pcg_node_settings: "Params: assetPath (or path), nodeName, settings OR propertyName + propertyValue",
  set_pcg_subgraph: "Params: assetPath (or path), nodeName, subgraphPath",
  set_pcg_subgraph_parameters: "Params: assetPath (or path), nodeName, parameters",
  set_static_mesh_spawner_meshes: "Params: assetPath (or path), nodeName, entries, replace?",
  toggle_pcg_graph: "Params: actorLabel OR actorPath, graphPath?",
  unwrap_pcg_instance_nodes: "Params: assetPath (or path), nodeName?",
  update_pcg_level_assets: "Params: assetPaths, save?",
};

/** Every key the spec'd pcg handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  actorLabel: z.string().optional().describe("Editor label of the actor holding the PCG component; a label naming several actors is refused"),
  actorPath: z.string().optional().describe("Full actor object path; the unambiguous selector"),
  assetName: z.string().optional().describe("Asset name (default: <LevelName>_PCG)"),
  assetPath: z.string().optional().describe("PCGGraph asset path (add_pcg_node, connect_pcg_nodes, disconnect_pcg_nodes, export_pcg_graph, import_pcg_graph, read_pcg_graph, read_pcg_node_settings, remove_pcg_node, set_pcg_graph_pins, set_pcg_node_settings, set_pcg_subgraph, set_pcg_subgraph_parameters, set_static_mesh_spawner_meshes, unwrap_pcg_instance_nodes). The PCG data asset's own path (an object path, or an existing asset's package path, updated in place), or else the folder for it (default: the level's folder) (export_level_to_pcg_asset)"),
  assetPaths: z.array(z.string()).optional().describe("PCG data assets to re-export from their source levels"),
  connections: z.array(z.record(z.unknown())).optional().describe("[{from, fromPin?, to, toPin?}]"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the 'nextCursor' from the previous page, unmodified"),
  entries: z.array(z.object({ mesh: z.string().describe("StaticMesh, package or object path; an entry without one is skipped, and one that does not load refuses the call"), weight: z.number().optional().describe("Relative pick weight (default 1), truncated to a whole number") })).optional().describe("Weighted mesh entries"),
  extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Half-size of the volume box {x,y,z} before scale (default 500 on each axis)"),
  graphPath: z.string().optional().describe("PCGGraph to assign to the volume's component (add_pcg_volume). PCGGraph to assign (default: re-apply the component's current graph) (toggle_pcg_graph)"),
  includeSettings: z.boolean().optional().describe("Include per-node editable settings in the response (default true)"),
  label: z.string().optional().describe("Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated"),
  levelPath: z.string().optional().describe("Saved level (.umap) to export, package or object path"),
  limit: z.number().int().optional().describe("Rows to return on this page (default 200, max 2000)"),
  location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World location {x,y,z} (default origin); wins over transform.location"),
  mode: z.string().optional().describe("append (default: add, or update a pin with the same label) | replace (the pins become exactly this list) | remove (labels only)"),
  name: z.string().optional().describe("Engine name for the node (default: engine-assigned). An existing node of the same class with this name is reported as existed; another class is refused (add_pcg_node). Graph asset name (create_pcg_graph)"),
  nodeName: z.string().optional().describe("Engine name of the node, as read_graph reports it (read_pcg_node_settings, remove_pcg_node, set_pcg_node_settings, set_static_mesh_spawner_meshes). Engine name of the Subgraph node, as read_graph reports it (set_pcg_subgraph). Engine name of the Subgraph node (set_pcg_subgraph_parameters). Only this node (default: every node in the graph) (unwrap_pcg_instance_nodes)"),
  nodes: z.array(z.record(z.unknown())).optional().describe("[{name, class, posX?, posY?, settings?}]"),
  nodeType: z.string().optional().describe("PCG settings class of the node to add"),
  onConflict: z.string().optional().describe("When the label exists: skip (default, report it) | error (add_pcg_volume). When the graph exists: skip (default, report it) | error (create_pcg_graph)"),
  packagePath: z.string().optional().describe("Folder for the new graph (default /Game/PCG)"),
  parameters: z.record(z.unknown()).optional().describe("{parameterName: value}; null clears that override"),
  path: z.string().optional().describe("Alias for assetPath (add_pcg_node, connect_pcg_nodes, disconnect_pcg_nodes, export_pcg_graph, import_pcg_graph, read_pcg_graph, read_pcg_node_settings, remove_pcg_node, set_pcg_graph_pins, set_pcg_node_settings, set_pcg_subgraph, set_pcg_subgraph_parameters, set_static_mesh_spawner_meshes, unwrap_pcg_instance_nodes). Alias for packagePath (create_pcg_graph)"),
  pins: z.array(z.object({ label: z.string().describe("Pin label; edges are wired by it"), type: z.string().optional().describe("EPCGDataType name: Any (default for a new pin), Point, Spatial, Param, PolyLine, Surface...; omitted keeps an existing pin's type"), tooltip: z.string().optional().describe("Pin tooltip"), allowMultipleData: z.boolean().optional().describe("Accept more than one data (default true)"), allowMultipleConnections: z.boolean().optional().describe("Accept more than one edge (default true)") })).optional().describe("Pins matched by label"),
  posX: z.number().optional().describe("Graph editor X position for the new node"),
  posY: z.number().optional().describe("Graph editor Y position for the new node"),
  propertyName: z.string().optional().describe("One property to write instead of a settings object"),
  propertyValue: z.string().optional().describe("The value for propertyName, as UE export text"),
  removeComponents: z.boolean().optional().describe("Remove the managed spawned components too (default true)"),
  removeConnections: z.array(z.record(z.unknown())).optional().describe("Without replace: edges [{from, fromPin, to, toPin}] to cut before importing"),
  removeNodes: z.array(z.string()).optional().describe("Without replace: node names to remove before importing; a missing name is skipped"),
  replace: z.boolean().optional().describe("Wipe existing user nodes first (default false) (import_pcg_graph). Overwrite existing MeshEntries (default true) (set_static_mesh_spawner_meshes)"),
  save: z.boolean().optional().describe("Save the asset after export (default true) (export_level_to_pcg_asset). Save the assets after export (default true) (update_pcg_level_assets)"),
  seed: z.number().int().optional().describe("Write the component's Seed before generating; the old one is reported as previousSeed"),
  settings: z.record(z.unknown()).optional().describe("{propertyPath: value}; dotted paths and nested structs supported"),
  side: z.string().optional().describe("input | output: the graph node whose pins change"),
  sourceNode: z.string().optional().describe("Node the edge leaves"),
  sourceNodeName: z.string().optional().describe("Alias for sourceNode"),
  sourcePin: z.string().optional().describe("Output pin label. connect_nodes defaults to the first output pin, disconnect_nodes to any"),
  sourcePinLabel: z.string().optional().describe("Alias for sourcePin"),
  subgraphPath: z.string().optional().describe("PCGGraph or PCGGraphInstance to run; \"\" clears it"),
  targetNode: z.string().optional().describe("Node the edge enters"),
  targetNodeName: z.string().optional().describe("Alias for targetNode"),
  targetPin: z.string().optional().describe("Input pin label. connect_nodes defaults to the first input pin, disconnect_nodes to any"),
  targetPinLabel: z.string().optional().describe("Alias for targetPin"),
  transform: z.object({ location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("World location"), rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional().describe("World rotation"), scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Actor scale (default 1)"), scale3D: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional().describe("Same as scale") }).optional().describe("Actor transform; scale multiplies the extent"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
