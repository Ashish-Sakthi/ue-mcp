# Test scripts

A test script is an ordered list of steps run against a Play In Editor session. Each step is an instruction for the person playing, and either an expectation the bridge checks against observed values (`judge: "auto"`) or a question the person answers (`judge: "manual"`). A run arms the observer and the recorder, so a finished run can be replayed later and its auto steps checked again against the same inputs.

Every action on this page is in the `pie` category.

## Steps

```json
{
  "instruction": "Walk up to the door",
  "judge": "auto",
  "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 }
}
```

| Field | Type | Meaning |
|---|---|---|
| `instruction` | string | What the person does, or what they check. Shown on the verdict form for a manual step. |
| `judge` | `auto` or `manual` | `auto`: the bridge decides from `expect`. `manual`: the step waits for a verdict. |
| `expect.value` | string | The observed value to check, as the observation profile names it. Auto steps only. |
| `expect.op` | string | One of the ops below. |
| `expect.target` | number or boolean | What `eq`, `gt` and `lt` compare with. For `visible` and `exists`, `false` asks for the opposite. |
| `expect.within_ms` | integer | How long the step has to meet its expectation, from the moment it starts. Defaults to the run's `default_within_ms` (5000). |

### Ops

| Op | Passes when |
|---|---|
| `decreasing` | The value at the end of the window is lower than at its start. |
| `increasing` | The value at the end of the window is higher than at its start. |
| `eq` | The value equals `target` at some sample in the window. |
| `gt` | The value is greater than `target` at some sample in the window. |
| `lt` | The value is less than `target` at some sample in the window. |
| `visible` | The observed actor is visible (or hidden, with `target: false`). |
| `exists` | The observed actor or value exists (or does not, with `target: false`). |

An auto step that does not meet its expectation within `within_ms` fails. A manual step waits until it gets a verdict.

## Storing scripts

Scripts are data assets, saved under `/Game/PIEStudio/Scripts` unless you give another package path. A script holds `steps`, an optional `description`, and an optional `profile`: the observation profile asset whose values the auto steps read.

| Action | Params |
|---|---|
| `script_create` | `name`, `steps`, `package_path?`, `description?`, `profile?`, `onConflict?` (`skip` or `replace`) |
| `script_read` | `path`. Returns `path`, `name`, `description`, `profile` and `steps`. |
| `script_update` | `path`, and any of `steps`, `description`, `profile` |
| `script_delete` | `path`, `confirm` |
| `script_list` | `directory?` |

```text
pie(action="script_create", name="DoorOpens", profile="/Game/PIEStudio/Profiles/Door",
    steps=[
      {"instruction": "Walk up to the door", "judge": "auto",
       "expect": {"value": "Door.Distance", "op": "decreasing", "within_ms": 3000}},
      {"instruction": "The door is open", "judge": "auto",
       "expect": {"value": "Door.IsOpen", "op": "eq", "target": true}},
      {"instruction": "The door opens without a visible pop", "judge": "manual"}
    ])
```

## Running a script

```text
pie(action="script_run", script="/Game/PIEStudio/Scripts/DoorOpens", start_pie=true, wait=true)
```

`script_run` takes a stored script (`script`) or inline `steps`. With inline steps, `name` and `profile` name the run and its observation profile.

| Param | Default | Meaning |
|---|---|---|
| `script` or `steps` | | The script asset path, or the steps themselves. |
| `profile` | the script's | The observation profile to arm. |
| `record` | `true` | Arm the recorder too, so the run can be replayed. `record=false` skips it, and the run cannot be replayed. |
| `start_pie` | `false` | Start PIE when it is not running. |
| `block_on_manual` | `true` | Hold the run at a manual step until it has a verdict. `false` moves on and leaves the verdict for later. |
| `default_within_ms` | `5000` | The window for an auto step that names none. |
| `pie_instance`, `client_id` | | Which PIE world and client to observe. |
| `wait` | `false` | Hold the answer until the run finishes, stops, or needs a verdict that nobody was asked for. |
| `timeout_seconds` | `600` | How long `wait` holds the answer. The run keeps going after it. |

The answer carries `run_id`, `state`, `output_dir`, `results_path`, `observation_run_id`, `recording_id` (when recording) and `steps`. With `wait=true` it also carries the final status and a `wait` object:

```json
{
  "run_id": "script-20261023-100000-1",
  "state": "finished",
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0 },
  "wait": { "state": "finished", "durationMs": 41250, "elicitation": "accepted" }
}
```

`wait.state` is one of:

| State | Meaning |
|---|---|
| `finished` | Every step has a result. |
| `stopped` | `script_stop` ended the run. |
| `awaiting_verdict` | Manual steps wait for `pie(verdict)`. `wait.awaiting` lists their indices, and the answer carries `verdictHint`. |
| `timed_out` | `timeout_seconds` ran out. The run is still going. |
| `unreachable` | The editor stopped answering while the server followed the run. |

### Status

`script_status` reports a run: `run_id`, `state`, `current_step`, `steps`, `awaiting`, `events`, `last_seq`, and once finished `results_path` and `summary`. Run states are `armed`, `running`, `awaiting_verdict`, `finished`, `stopped` and `idle`. Step statuses are `pending`, `running`, `passed`, `failed`, `awaiting_verdict` and `skipped`.

`since_seq` returns only the events after that number, so a caller that polls passes the `last_seq` it last saw. `wait` and `timeout_seconds` work as they do on `script_run`. Calling `script_status` on a run that has not ended also makes the server follow it again, so its events reach the daemon stream after a server restart.

### Stopping

`script_stop` (`run_id?`, `reason?`) ends the run, writes `results.json`, and returns the summary. Steps without a result stay `pending`.

## Manual steps and verdicts

When the run reaches manual steps, the server puts them to the person in one form, if the MCP client advertises the elicitation capability. Each step has a choice of `pass`, `fail` or `skip` and an optional note. The answers are posted as the steps' verdicts, with `verdict_source: "elicitation"`.

The form is asked once per step. When the client cannot show it, or the person declines or cancels it, or the client answers faster than a person could (`UE_MCP_ELICIT_MIN_HUMAN_MS`, default 1000), the steps are left for the `verdict` action and are not asked about again in that run. A step left blank on an accepted form is also left for `verdict`.

```text
pie(action="verdict", step=2, verdict="pass", note="No pop on open")
```

| Param | Meaning |
|---|---|
| `step` | The step's 0-based index. |
| `verdict` | `pass`, `fail` or `skip`. |
| `note` | Optional. Kept in `results.json`. |
| `run_id` | Optional. Defaults to the current run. |

A verdict given this way is recorded with `verdict_source: "action"`.

## Results

`script_results` (`run_id?` or `results_path?`) returns `results.json`, which the run writes beside `observation.csv` in `Saved/MCPObservations/<run_id>/`:

```json
{
  "version": 1,
  "run_id": "script-20261023-100000-1",
  "script": "/Game/PIEStudio/Scripts/DoorOpens",
  "profile": "/Game/PIEStudio/Profiles/Door",
  "started_at": "2026-10-23T10:00:00.000Z",
  "ended_at": "2026-10-23T10:00:41.250Z",
  "state": "finished",
  "recording_id": "rec-20261023-100000",
  "recording_dir": "C:/MyGame/Saved/MCPRecordings/rec-20261023-100000",
  "summary": { "total": 3, "passed": 2, "failed": 1, "skipped": 0, "pending": 0 },
  "steps": [
    {
      "index": 0,
      "instruction": "Walk up to the door",
      "judge": "auto",
      "expect": { "value": "Door.Distance", "op": "decreasing", "within_ms": 3000 },
      "status": "passed",
      "started_at_ms": 0,
      "ended_at_ms": 2210,
      "started_at": "2026-10-23T10:00:00.000Z",
      "ended_at": "2026-10-23T10:00:02.210Z",
      "evidence": { "value": "Door.Distance", "at_start": 412.5, "at_end": 38.1, "samples": 133 },
      "verdict_source": "auto"
    },
    {
      "index": 2,
      "instruction": "The door opens without a visible pop",
      "judge": "manual",
      "status": "failed",
      "started_at_ms": 5400,
      "ended_at_ms": 41250,
      "started_at": "2026-10-23T10:00:05.400Z",
      "ended_at": "2026-10-23T10:00:41.250Z",
      "evidence": { "value": "", "at_start": null, "at_end": null, "samples": 0 },
      "note": "Pops on the second frame",
      "verdict_source": "elicitation"
    }
  ]
}
```

A replay run also carries `replay_of`, the run it replays, and a comparison of each step with the source run.

## Replay regression

`script_replay` (`run_id?` or `results_path?`, `time_scale?`, `auto_stop_pie?` default `true`, `pie_instance?`, `client_id?`, `wait?`, `timeout_seconds?`) replays the run's recording with the same observation profile and checks every auto step again at its recorded offset. It returns a new `run_id`; its results carry `replay_of` and the per-step comparison. A run made with `record=false` has no recording and cannot be replayed.

```text
pie(action="script_replay", run_id="script-20261023-100000-1", wait=true)
```

## Daemon events

The daemon publishes every step of every run on its event stream, `GET /v1/events`, as the server reads it from the editor. A client that did not start the run sees it too.

| Event | When | Payload beyond the common fields |
|---|---|---|
| `script.run_started` | The run began. | `resultsPath?` |
| `script.step_started` | A step began. | `step` |
| `script.step_passed` | An auto step met its expectation. | `step` |
| `script.step_failed` | An auto step did not. | `step` |
| `script.verdict_requested` | Manual steps wait for a verdict. | `awaiting`: `[{ index, instruction }]` |
| `script.verdict` | A manual step got its verdict. | `step`, with `verdictSource` |
| `script.run_finished` | The run finished or was stopped. | `summary`, `state?`, `resultsPath?` |

Every payload carries `runId`, `script` (the asset path, or `inline`), `at` (ISO wall time), `atMs` (milliseconds since the run started), `seq` (the editor's number for the event, increasing within a run) and, for a replay, `replayOf`. `step` is `{ index, instruction, judge, expect?, status, evidence?, note?, verdictSource? }`.

```text
id: 412
event: script.step_passed
data: {"at":"2026-10-23T10:00:02.210Z","runId":"script-20261023-100000-1","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":2210,"seq":3,"step":{"index":0,"instruction":"Walk up to the door","judge":"auto","status":"passed","expect":{"value":"Door.Distance","op":"decreasing","within_ms":3000},"evidence":{"value":"Door.Distance","at_start":412.5,"at_end":38.1,"samples":133},"verdictSource":"auto"}}

id: 413
event: script.verdict_requested
data: {"at":"2026-10-23T10:00:05.400Z","runId":"script-20261023-100000-1","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":5400,"seq":6,"awaiting":[{"index":2,"instruction":"The door opens without a visible pop"}]}

id: 420
event: script.run_finished
data: {"at":"2026-10-23T10:00:41.250Z","runId":"script-20261023-100000-1","script":"/Game/PIEStudio/Scripts/DoorOpens","atMs":41250,"seq":9,"state":"finished","summary":{"total":3,"passed":2,"failed":1,"skipped":0,"pending":0},"resultsPath":"C:/MyGame/Saved/MCPObservations/script-20261023-100000-1/results.json"}
```

To subscribe, read the daemon's port and token from `~/.ue-mcp/daemons/<key>.json` and open the stream with the token:

```bash
curl -N -H "Authorization: Bearer $TOKEN" http://127.0.0.1:$PORT/v1/events
```

A browser `EventSource`, which cannot set headers, passes `?token=` instead. To resume after a disconnect, send the last `id:` you saw as the `Last-Event-ID` header (or `?since=`); the daemon replays what came after it. When some of those events have already dropped out of its window, it sends a `gap` event first, and the client reads the run's state with `pie(script_status)`.

A daemon publishes each event of a run once. A new daemon process that picks up a run in progress (after `pie(script_status)` on it) reads it from its first event again, so a client that keeps state across a daemon restart ignores an event whose `runId` and `seq` it has already seen.
