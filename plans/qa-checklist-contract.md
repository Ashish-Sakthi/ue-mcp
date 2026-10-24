# QA checklist client contract

The contract a client builds a QA checklist UI against: a list of test script steps that fills in live as a run progresses, with pass, fail and skip controls on the manual steps. It names what ue-mcp guarantees and nothing about how any one client draws it.

A client reaches the run three ways, and may use any mix:

1. MCP tool calls on the `pie` category (any MCP client).
2. The MCP elicitation form the server raises for manual steps (an MCP client that advertises elicitation).
3. The daemon's event stream, `GET /v1/events`, and, for a daemon extension, `api.callAction`.

All field names on the wire are snake_case in tool params, tool answers and `results.json`. Event payloads on `/v1/events` are camelCase, apart from the `expect` and `evidence` objects, which pass through as the editor wrote them.

## 1. Actions

Every action is `pie(<action>, ...)`. Through the micro gateway it is `tools(action="call", category="pie", method="<action>", args={...})`; through a daemon extension it is `api.callAction("pie", "<action>", {...})`.

The editor holds one run: the active one, or the last one to end. `script_run` and `script_replay` are refused while a run is active. `script_status` and `verdict` act only on the run the editor holds; read an older run with `script_results`.

### Ids, paths and times

- A run id is `script_<name>_<YYYYMMDD_HHMMSS>` (editor local time), and a replay run's is `script_replay_<name>_<YYYYMMDD_HHMMSS>`. Characters other than letters, digits, `-` and `_` in the name become `_`. When the folder already exists, `_2`, `_3` and so on are appended.
- `observation_run_id` and `recording_id` equal `run_id`. A replay run's `recording_id` is the source run's.
- Paths are absolute: `output_dir` is `<Project>/Saved/MCPObservations/<run_id>`, `results_path` is `<output_dir>/results.json`, `recording_dir` is `<Project>/Saved/MCPRecordings/<run_id>`.
- `*_ms` times on steps and events (`started_at_ms`, `ended_at_ms`, `at_ms`, `elapsed_ms`, `window_start_ms`, `window_end_ms`) are game milliseconds from the observer's first frame. `verdict_at_ms` is game milliseconds from the step's start. Wall times (`started_at`, `ended_at`, `wall_time`, `at`) are ISO 8601 UTC.

### pie(script_run)

Params:

| Param | Type | Default | |
|---|---|---|---|
| `script` | string | | Script asset path. Exactly one of `script` or `steps` is required. |
| `steps` | Step[] | | Inline steps (section 5). |
| `name` | string | the asset name, or `inline` | Run name. Also overrides the asset name. |
| `profile` | string | the script's | Observation profile asset path. |
| `record` | boolean | `true` | Arm the recorder. `false` makes the run not replayable. |
| `start_pie` | boolean | `false` | Start PIE when it is not running. |
| `block_on_manual` | boolean | `true` | Hold at a manual step, or a block of consecutive manual steps, until each has a verdict. |
| `default_within_ms` | integer | the script's, or `5000` | Window for an auto step without `within_ms`. |
| `pie_instance` | integer | the active PIE world | PIE world to observe. |
| `client_id` | integer | `0` | Local player in that world. |
| `wait` | boolean | `false` | Hold the answer (section 1.7). |
| `timeout_seconds` | number | `600` | How long `wait` holds it. |

Answer (abridged: it also carries `created`, `existed`, `rollback` and `message`):

```json
{
  "success": true,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "state": "armed",
  "current_step": 0,
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "recording_id": "script_DoorOpens_20261023_100000",
  "elapsed_ms": 0,
  "steps": [
    { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }, "status": "pending", "within_ms": 3000 },
    { "index": 1, "instruction": "Open the door", "judge": "auto", "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 }, "status": "pending", "within_ms": 5000 },
    { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "status": "pending", "within_ms": 5000 }
  ],
  "awaiting": [],
  "events": [
    { "seq": 1, "type": "run_started", "run_id": "script_DoorOpens_20261023_100000", "step": -1, "at_ms": 0, "wall_time": "2026-10-23T15:00:00.000Z",
      "data": { "script": "/Game/PIEStudio/Scripts/DoorOpens", "script_name": "DoorOpens", "steps": 3, "observation_run_id": "script_DoorOpens_20261023_100000", "recording_id": "script_DoorOpens_20261023_100000" } }
  ],
  "last_seq": 1,
  "summary": { "total": 3, "passed": 0, "failed": 0, "skipped": 0, "pending": 3, "awaiting": 0 },
  "pie_started": true,
  "pie_running": false
}
```

`recording_id` is absent with `record=false`. The run moves from `armed` to `running` on the observer's first frame. A successful answer always starts the server following the run, so its events reach `/v1/events` whether or not the caller waits.

### pie(script_status)

Params: `run_id?` (default: the run the editor holds), `since_seq?` (integer), `wait?`, `timeout_seconds?`.

Answer: the same fields as the `script_run` answer, plus `active`, with `events` after `since_seq`:

```json
{
  "success": true,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "state": "awaiting_verdict",
  "current_step": 2,
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "recording_id": "script_DoorOpens_20261023_100000",
  "elapsed_ms": 2210,
  "steps": [
    { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }, "status": "passed", "within_ms": 3000,
      "started_at_ms": 33.3, "ended_at_ms": 50, "started_at": "2026-10-23T15:00:00.412Z", "ended_at": "2026-10-23T15:00:00.429Z",
      "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 }, "verdict_source": "auto" },
    { "index": 1, "instruction": "Open the door", "judge": "auto", "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 }, "status": "passed", "within_ms": 5000,
      "started_at_ms": 50, "ended_at_ms": 2210, "started_at": "2026-10-23T15:00:00.429Z", "ended_at": "2026-10-23T15:00:02.590Z",
      "evidence": { "value": "Door.OpenAngle", "at_start": 0, "at_end": 81.4, "samples": 130, "observed": true, "verdict_frame": 133, "verdict_at_ms": 2160 }, "verdict_source": "auto" },
    { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "status": "awaiting_verdict", "within_ms": 5000,
      "started_at_ms": 2210, "started_at": "2026-10-23T15:00:02.590Z" }
  ],
  "awaiting": [{ "index": 2, "instruction": "The door opens without a visible pop" }],
  "events": [
    { "seq": 6, "type": "step_started", "run_id": "script_DoorOpens_20261023_100000", "step": 2, "at_ms": 2210, "wall_time": "2026-10-23T15:00:02.590Z",
      "data": { "status": "awaiting_verdict", "instruction": "The door opens without a visible pop", "judge": "manual" } },
    { "seq": 7, "type": "awaiting_verdict", "run_id": "script_DoorOpens_20261023_100000", "step": 2, "at_ms": 2210, "wall_time": "2026-10-23T15:00:02.590Z",
      "data": { "awaiting": [2], "blocking": true, "steps": [{ "index": 2, "instruction": "The door opens without a visible pop" }] } }
  ],
  "last_seq": 7,
  "summary": { "total": 3, "passed": 2, "failed": 0, "skipped": 0, "pending": 1, "awaiting": 1 },
  "active": true
}
```

- `state`: `armed` | `running` | `awaiting_verdict` | `finished` | `stopped` | `idle`. A run the editor no longer holds in memory answers from its `results.json` (events included) with `live: false`. `idle` means no such run exists on disk either (or there has been no run yet): the answer then has only `state`, `run_id` (when given), empty `steps`, `awaiting` and `events`, and `last_seq` equal to `since_seq`.
- step `status`: `pending` | `running` | `passed` | `failed` | `awaiting_verdict` | `skipped`. A manual step ends as `passed`, `failed` or `skipped` from its verdict.
- step fields: `index`, `instruction`, `judge`, `expect?`, `status`, `within_ms`, then once set `started_at_ms`, `ended_at_ms`, `started_at`, `ended_at`, `evidence`, `note`, `verdict_source`. Replay steps add `source_status`, `regression`, and for auto steps `window_start_ms` and `window_end_ms` (section 4).
- `awaiting`: the steps waiting for a verdict, as `{ index, instruction }`.
- `summary`: `total`, `passed`, `failed`, `skipped`, `pending`, `awaiting`, and `regressions` on a replay. `pending` counts every step not settled, awaiting ones included.
- `events`: the events with `seq > since_seq` (all of them without `since_seq`), oldest first. Each is `{ seq, type, run_id, step, at_ms, wall_time, data }`; `step` is the step index, or `-1` for a run event and for an `awaiting_verdict` that covers several steps.
- `last_seq`: the highest seq the run has. Pass it as the next `since_seq`.

Editor event types and their `data`:

| Type | `data` |
|---|---|
| `run_started` | `script`, `script_name`, `steps` (the step count), `observation_run_id`, `recording_id?`, and `replay_of` on a replay |
| `step_started` | `status`, `instruction`, `judge`, and for a step with `expect`: `expect`, `within_ms`; on a replay `window_start_ms`, `window_end_ms` |
| `step_passed`, `step_failed` | `status`, `evidence` (or `reason` for a step without `expect`), `regression: true` on a replay regression |
| `awaiting_verdict` | `awaiting` (indices), `blocking` (false with `block_on_manual=false`), `steps` (`[{ index, instruction }]`) |
| `verdict` | `status`, `verdict`, `note?`, `verdict_source` |
| `run_finished` | `state`, `reason`, `summary`, `results_path`, `awaiting?` (indices still waiting) |

A polling client calls `script_status(since_seq=last_seq)` and applies `events` in order. Calling it on a run that has not ended also restarts the server's follower, so a client can call it once after a server restart to get the daemon stream going again.

### pie(verdict)

Params: `step` (integer, 0-based, required), `verdict` (`pass` | `fail` | `skip`, required), `note?` (string), `run_id?` (default: the run the editor holds), `source?` (`action` | `elicitation`, default `action`).

Answer: `run_id`, `step`, `status`, `verdict`, `verdict_source`, `note?`, `state` and `awaiting` (the indices still waiting). A repeat of the same verdict and note answers `unchanged: true`. The verdict is refused with `success: false` and an `error` for an auto step, a step that has not started, a step index out of range, or a `run_id` the editor does not hold:

```json
{ "success": false, "error": "Run script_DoorOpens_20261023_100000 is not the current run (script_DoorOpens_20261023_100200)" }
```

A verdict can replace an earlier one, and is accepted on a run that has ended, which rewrites its `results.json`. It appears on the stream as `script.verdict` once the server's next poll reads it, while the server follows the run.

### pie(script_stop)

Params: `run_id?` (must be the active run when given), `reason?`. Ends the active run with `state: "stopped"` and `stop_reason` set to `reason` (default `stopped`), writes `results.json`, and answers with the run's fields (including `summary` and `results_path`), `stopped: true` and `changed: true`. An auto step still running fails; pending steps stay `pending` and manual steps without a verdict stay `awaiting_verdict`. With no active run it answers `success: false`, `alreadyStopped: true`, `changed: false`.

### pie(script_results)

Params: `run_id?` or `results_path?`. Answers with `results.json` (section 4) plus `success` and `live`. For the active run it is built from memory (`live: true`); otherwise it is read from `results_path`, or `<Project>/Saved/MCPObservations/<run_id>/results.json`.

### pie(script_replay)

Params: `run_id?` (default: the last run) or `results_path?`, `time_scale?` (default 1), `auto_stop_pie?` (default `true`), `pie_instance?`, `client_id?`, `wait?`, `timeout_seconds?`.

Refused while PIE is running, and for a source run made with `record=false`. Starts PIE, replays the source run's recording with the same observation profile, and evaluates every auto step again in its recorded window. Answers like `script_run`, with a new `run_id`, plus `replay_of`, `pie_started`, `auto_stop_pie` and `time_scale`. That run's events carry `replayOf`, and its results carry `replay_of` and the per-step comparison of section 4.

### 1.7 wait

With `wait: true`, `script_run`, `script_replay` and `script_status` hold the answer and return it merged with the run's final status and a `wait` object:

```json
{
  "wait": {
    "state": "awaiting_verdict",
    "durationMs": 5400,
    "awaiting": [2],
    "elicitation": "unavailable"
  },
  "verdictHint": "call pie(verdict, step, verdict, note?)"
}
```

| Field | Type | |
|---|---|---|
| `state` | `finished` \| `stopped` \| `awaiting_verdict` \| `timed_out` \| `unreachable` | Why the wait ended. |
| `durationMs` | integer | How long it waited. |
| `awaiting` | integer[] | With `awaiting_verdict`: the steps waiting for `pie(verdict)`. |
| `elicitation` | `accepted` \| `declined` \| `cancelled` \| `auto_answered` \| `failed` \| `unavailable` | How the last verdict form went. Absent when no form was due. |
| `error` | string | With `timed_out` or `unreachable`, and with `stopped` when the editor no longer holds the run. |

- `awaiting_verdict` is returned only when no form can or will collect the verdict: the client has no elicitation, or the form for those steps was declined, cancelled, answered without being shown, or failed. While a form is up, the wait goes on.
- `timed_out` does not stop the run.
- `stopped` also covers a run id the editor has no record of (its status is `idle`).
- `unreachable` means the editor failed to answer five status polls in a row, or the six hour follow limit ran out.
- `verdictHint` is present exactly when `wait.state` is `awaiting_verdict`.
- On `script_run` and `script_replay` the merged answer has no `events`; read them from `script_status` or the stream. On `script_status` the merged `events` honour the caller's `since_seq`.

## 2. The verdict form (MCP elicitation)

Raised by the server when a run's status shows manual steps awaiting a verdict and the connected MCP client advertised the `elicitation` capability. One form covers every step awaiting at that moment that has not been asked about before in this run.

```json
{
  "message": "Test script /Game/PIEStudio/Scripts/DoorOpens (run script_DoorOpens_20261023_100000) needs your verdict on one manual step:\n\nStep 3: The door opens without a visible pop\n\nPick pass, fail or skip for each. A step left blank waits for pie(verdict).",
  "requestedSchema": {
    "type": "object",
    "properties": {
      "step_2": {
        "type": "string",
        "title": "Step 3",
        "description": "The door opens without a visible pop",
        "enum": ["pass", "fail", "skip"]
      },
      "step_2_note": {
        "type": "string",
        "title": "Step 3 note",
        "description": "Optional. What you saw."
      }
    }
  }
}
```

- The message names the script as the asset path, or `inline`, and says `<n> manual steps` for more than one.
- Property names: `step_<i>` and `step_<i>_note`, where `<i>` is the 0-based step index. Titles number from 1 (`Step <i+1>`).
- `step_<i>` is a string enum of exactly `pass`, `fail`, `skip`. `step_<i>_note` is a free string.
- No property is required. The schema has no `required` key.

Answers:

| Answer | Effect |
|---|---|
| `accept` | For each `step_<i>` holding `pass`, `fail` or `skip`, the server calls `pie_script_verdict { run_id, step: i, verdict, source: "elicitation", note? }`. The note is trimmed and sent only when not empty. A step left blank stays awaiting. |
| `decline`, `cancel` | Nothing is posted. The steps stay awaiting. |
| any answer other than `accept` sooner than `UE_MCP_ELICIT_MIN_HUMAN_MS` (default 1000 ms) | Taken as a client that did not show the form (`auto_answered`). Nothing is posted. |
| the request throws | `failed`. Nothing is posted. |

Every step put to a form is never put to a form again in the same run, whatever the answer. A step the form did not settle is settled only by `pie(verdict)`. Without elicitation, no form is raised, and `script.verdict_requested` on the stream plus `wait.state: "awaiting_verdict"` are the signals to collect the verdict another way.

## 3. Events on /v1/events

### Transport

- `GET http://127.0.0.1:<port>/v1/events`, with `Authorization: Bearer <token>`. Port and token are in the daemon's discovery file, `~/.ue-mcp/daemons/<key>.json`. A browser `EventSource` passes `?token=<token>`.
- Server-sent events. Each frame is `id: <n>`, `event: <type>`, `data: <json>`. `id` is the daemon's stream id, increasing by one across every event type.
- Resume with `Last-Event-ID: <n>` (or `?since=<n>`): the daemon replays what it still holds after `n`, then streams live. It holds the last 1000 events. When some events after `n` have dropped out, it first sends `event: gap` with `data: {}`; the client then rebuilds the run with `pie(script_status)` without `since_seq`, or `pie(script_results)` for an ended run.
- `data` always has `at` (ISO 8601). For script events it is the editor's wall time of the event.

### Event types

`script.run_started`, `script.step_started`, `script.step_passed`, `script.step_failed`, `script.verdict_requested`, `script.verdict`, `script.run_finished`. `script.verdict_requested` is the editor's `awaiting_verdict`; the others keep the editor's names. A client ignores any other `script.*` type it does not know.

### Common fields

| Field | Type | |
|---|---|---|
| `runId` | string | The run. |
| `script` | string | Script asset path, or `"inline"`. |
| `at` | string | ISO 8601 wall time, from the editor. |
| `atMs` | number | The event's `at_ms`: game milliseconds from the observer's first frame. `0` on `run_started`, which comes before it. |
| `seq` | integer | The editor's number for the event, increasing within the run. |
| `replayOf` | string? | For a replay run, the source run id. |

### Step object

Carried as `step` by `step_started`, `step_passed`, `step_failed` and `verdict`:

| Field | Type | |
|---|---|---|
| `index` | integer | 0-based. |
| `instruction` | string | |
| `judge` | `"auto"` \| `"manual"` | |
| `expect` | object? | `{ value: string, op: "decreasing"\|"increasing"\|"eq"\|"gt"\|"lt"\|"visible"\|"exists", target?: number\|boolean, within_ms?: integer }` |
| `status` | string | The step's status after this event: `running` (or `awaiting_verdict` for a manual step) on `step_started`, `passed` on `step_passed`, `failed` on `step_failed`, and `passed`, `failed` or `skipped` on `verdict`. |
| `evidence` | object? | The editor's evidence object for an auto step with `expect`: `{ value, at_start, at_end, samples, observed, verdict_frame?, verdict_at_ms?, reason? }`. |
| `note` | string? | |
| `verdictSource` | `"auto"` \| `"action"` \| `"elicitation"` \| `"source_run"`, optional | Who decided the step. |

Fields the event itself does not carry are filled from the status the server read with it, so a `step_started` step can already hold `evidence` and `verdictSource`. The step object also carries `withinMs`, `startedAtMs` and `endedAtMs` once set, and on a replay `sourceStatus` and `regression`. The `run_finished` summary carries `awaiting`, and `regressions` on a replay.

### Per type

| Type | Extra fields |
|---|---|
| `script.run_started` | `resultsPath?: string` |
| `script.step_started` | `step` |
| `script.step_passed` | `step` |
| `script.step_failed` | `step` |
| `script.verdict_requested` | `awaiting: Array<{ index: integer, instruction: string }>` |
| `script.verdict` | `step` |
| `script.run_finished` | `summary: { total, passed, failed, skipped, pending, awaiting?, regressions? }` (integers), `state?: "finished" \| "stopped"`, `resultsPath?: string` |

### Examples

```json
{ "at": "2026-10-23T15:00:00.000Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 0, "seq": 1, "resultsPath": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json" }
```

```json
{ "at": "2026-10-23T15:00:00.429Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 50, "seq": 3,
  "step": { "index": 0, "instruction": "Walk up to the door", "judge": "auto", "status": "passed",
            "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
            "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 },
            "verdictSource": "auto" } }
```

```json
{ "at": "2026-10-23T15:00:02.590Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 2210, "seq": 7,
  "awaiting": [{ "index": 2, "instruction": "The door opens without a visible pop" }] }
```

```json
{ "at": "2026-10-23T15:00:31.050Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 30671, "seq": 8,
  "step": { "index": 2, "instruction": "The door opens without a visible pop", "judge": "manual", "status": "failed", "note": "Pops on the second frame", "verdictSource": "elicitation" } }
```

```json
{ "at": "2026-10-23T15:00:31.050Z", "runId": "script_DoorOpens_20261023_100000", "script": "/Game/PIEStudio/Scripts/DoorOpens", "atMs": 30671, "seq": 9,
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0 }, "state": "finished",
  "resultsPath": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json" }
```

### Ordering and duplicates

- Within one run, events arrive in increasing `seq`, and each `seq` at most once per daemon process.
- A run's events do not arrive as they happen: the server polls the editor every 250 ms to 1 s, so several can arrive together. `atMs` and `at` carry the real timing.
- Runs interleave freely; filter on `runId`.
- `seq` may skip numbers when the editor records an event type this version does not publish.
- A new daemon process that picks up a run in progress reads it from its first event, so a client that keeps state across a daemon restart drops any event whose (`runId`, `seq`) it already holds.
- `verdict_requested` may come once per step or once for several steps; treat `awaiting` as the set of steps to show controls for, and clear a step's controls on its `verdict` event.
- The stream says nothing for a run nobody started following in this daemon. A client that wants a run on the stream calls `pie(script_status, run_id)` once, before the run ends.

## 4. results.json

Written to `<Project>/Saved/MCPObservations/<run_id>/results.json`, beside the observation files, when the run ends, and again when a verdict lands on an ended run.

```json
{
  "version": 1,
  "run_id": "script_DoorOpens_20261023_100000",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "script_name": "DoorOpens",
  "profile": "/Game/PIEStudio/Profiles/Door",
  "started_at": "2026-10-23T15:00:00.412Z",
  "ended_at": "2026-10-23T15:00:31.050Z",
  "state": "finished",
  "stop_reason": "all steps settled",
  "block_on_manual": true,
  "default_within_ms": 5000,
  "observation_run_id": "script_DoorOpens_20261023_100000",
  "output_dir": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000",
  "results_path": "C:/MyGame/Saved/MCPObservations/script_DoorOpens_20261023_100000/results.json",
  "recording_id": "script_DoorOpens_20261023_100000",
  "recording_dir": "C:/MyGame/Saved/MCPRecordings/script_DoorOpens_20261023_100000",
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0, "awaiting": 0 },
  "steps": [
    {
      "index": 0,
      "instruction": "Walk up to the door",
      "judge": "auto",
      "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
      "status": "passed",
      "within_ms": 3000,
      "started_at_ms": 33.3,
      "ended_at_ms": 50,
      "started_at": "2026-10-23T15:00:00.412Z",
      "ended_at": "2026-10-23T15:00:00.429Z",
      "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 411.9, "samples": 2, "observed": true, "verdict_frame": 3, "verdict_at_ms": 16.7 },
      "verdict_source": "auto"
    },
    {
      "index": 1,
      "instruction": "Open the door",
      "judge": "auto",
      "expect": { "value": "Door.OpenAngle", "op": "gt", "target": 80 },
      "status": "passed",
      "within_ms": 5000,
      "started_at_ms": 50,
      "ended_at_ms": 2210,
      "started_at": "2026-10-23T15:00:00.429Z",
      "ended_at": "2026-10-23T15:00:02.590Z",
      "evidence": { "value": "Door.OpenAngle", "at_start": 0, "at_end": 81.4, "samples": 130, "observed": true, "verdict_frame": 133, "verdict_at_ms": 2160 },
      "verdict_source": "auto"
    },
    {
      "index": 2,
      "instruction": "The door opens without a visible pop",
      "judge": "manual",
      "status": "failed",
      "within_ms": 5000,
      "started_at_ms": 2210,
      "ended_at_ms": 30671,
      "started_at": "2026-10-23T15:00:02.590Z",
      "ended_at": "2026-10-23T15:00:31.050Z",
      "note": "Pops on the second frame",
      "verdict_source": "elicitation"
    }
  ]
}
```

| Field | Type | |
|---|---|---|
| `version` | integer | `1`. |
| `run_id`, `script`, `script_name` | string | `script` is `"inline"` for inline steps. |
| `description` | string? | The script's description. Absent when empty. |
| `profile` | string | Empty when the run has none. |
| `started_at`, `ended_at` | ISO 8601 string | The observer's first frame, and the end. |
| `state` | `finished` \| `stopped` | |
| `stop_reason` | string | `all steps settled`, `PIE ended`, `observation ended`, or the `script_stop` reason (default `stopped`). |
| `block_on_manual` | boolean | `false` on a replay. |
| `default_within_ms` | integer | |
| `observation_run_id`, `output_dir`, `results_path` | string | Absolute paths. |
| `recording_id`, `recording_dir` | string? | Absent with `record=false`. The source run's on a replay. |
| `replay_of`, `time_scale`, `replay_offset_ms` | string?, number?, number? | Replay runs only. `replay_offset_ms` is the recording's settle time. |
| `summary` | `{ total, passed, failed, skipped, pending, awaiting, regressions? }` | Integers. `pending` includes awaiting steps. `regressions` on a replay only. |
| `steps[].status` | step status | `pending` or `awaiting_verdict` only on a run that ended before they settled. |
| `steps[].within_ms` | integer | The effective window. |
| `steps[].started_at_ms`, `ended_at_ms` | number? | Game ms from the observer's first frame. Absent until set. |
| `steps[].started_at`, `ended_at` | ISO 8601 string? | Absent until set. |
| `steps[].evidence` | object? | Auto steps with `expect`: `{ value, at_start, at_end, samples, observed, verdict_frame?, verdict_at_ms?, reason? }`, `at_start` and `at_end` `null` when never read. Other steps: `{ reason }` when there is one (`no expectation`, or a replay skip reason). Absent on manual steps otherwise. |
| `steps[].note` | string? | |
| `steps[].verdict_source` | `auto` \| `action` \| `elicitation` \| `source_run` | Absent until the step settles. |

Replay runs add to each step:

| Field | Type | |
|---|---|---|
| `source_status` | step status | The step's status in the source run. |
| `regression` | boolean | `true` when it passed in the source run and failed in the replay. |
| `window_start_ms`, `window_end_ms` | number? | Auto steps the source run reached: the source `started_at_ms` plus `replay_offset_ms`, and that plus `within_ms`. |

On a replay, manual steps carry the source verdict and note with `verdict_source: "source_run"`, or are `skipped` with reason `no verdict in the source run`. An auto step the source run never reached is `skipped` with reason `not reached in the source run`.

## 5. Step format (script_create, script_update, inline steps)

```json
{ "instruction": "string", "judge": "auto | manual", "expect": { "value": "string", "op": "decreasing | increasing | eq | gt | lt | visible | exists", "target": 0, "within_ms": 3000 } }
```

- `instruction` is required. `judge` defaults to `auto`.
- A manual step with `expect` is refused. An auto step without `expect` passes when it starts.
- `target` is required for `eq`, `gt` and `lt`, refused for `decreasing` and `increasing`, and defaults to `true` for `visible` and `exists`. It is a number or a boolean; `script_read` and results write it back as a number for `eq`, `gt` and `lt`, and as a boolean for `visible` and `exists`.
- `within_ms` 0 or absent uses the run's `default_within_ms`.

Storage actions: `script_create { name, steps, package_path?, description?, profile?, default_within_ms?, onConflict? (skip | update | replace | error) }`, `script_read { path }`, `script_update { path, steps?, description?, profile?, default_within_ms? }`, `script_delete { path, confirm: true }`, `script_list { directory? }`.

## 6. A daemon extension

An extension runs actions in process and reads the same event log:

```js
export function activate(api) {
  if (!(api.features ?? []).includes("events.script")) return; // an older daemon

  api.events.subscribe((e) => {
    if (!e.type.startsWith("script.")) return;
    // e.id, e.type, e.at, e.data (the payload in section 3)
  });

  api.route("POST", "/verdict", async (req) => {
    const { run_id, step, verdict, note } = req.body;
    const r = await api.callAction("pie", "verdict", { run_id, step, verdict, ...(note ? { note } : {}) });
    return { status: r.isError ? 409 : 200, body: r }; // r = { isError, content }: the MCP result blocks
  });
}
```

- `api.callAction("pie", "verdict", { run_id, step, verdict, note? })` posts a verdict exactly as an MCP client's `pie(verdict)` does, recorded with `verdict_source: "action"`.
- `api.callAction("pie", "script_run", {...}, { elicit })` puts the verdict form (section 2) to the extension's `elicit` instead of an MCP client, when the extension passes one (feature `callAction.elicit`). The follower keeps that `elicit` after the call returns, so the form can arrive later. Without it no form is raised, and the extension collects verdicts itself from `script.verdict_requested`.
- The extension never calls the bridge's `pie_script_*` methods directly; the action layer keeps the follower and the stream in step.

## 7. Versioning

- Additive. `DAEMON_API_VERSION` and `EXTENSION_API_VERSION` are unchanged.
- Feature flag `events.script` in the extension API's `api.features` marks a daemon that publishes these events. An MCP or HTTP client without the extension API detects it by the first `script.*` event, or by the ue-mcp version.
- New event types, new payload fields and new `wait.state` or `elicitation` values may be added without a version change. A client ignores what it does not know.
- Renaming or removing a field, an event type or a form property is a breaking change and bumps `EXTENSION_API_VERSION`.
- `results.json` changes additively within `version: 1`.
