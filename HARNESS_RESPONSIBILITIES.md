# Harness responsibilities

What each part of the AI pipeline is responsible for, what it guarantees, and what it is not
responsible for. `HARNESS.md` explains what the harness is and how it runs; this document is the
contract between its parts. When a change moves a responsibility, update this file.

## The parties

| Party | What it is | Code |
|---|---|---|
| **Model** | The language model of the active provider. Produces text, nothing else. | provider in `ai_config.lua` |
| **Harness** | The loop around the model: call, cleanup, checks, retries, review, approval. | `AiHarness.h/.cpp` |
| **Task** | One use of the harness: what to ask, how to validate, how to test-run. | `LuaCommands::aiCommand` (`ATAICMD`), `LuaTools::aiLuaCommand` (`ATAILUA`) |
| **Connector** | Talks to the provider: request format, auth, timeout, errors. | `AITools.cpp` + `AiConfig.h/.cpp` |
| **Sandbox** | Runs Lua with limits. | `LuaTools::LuaEngine` |
| **Tester** | Runs code unattended in a scratch drawing and reports what it did. | `CommandTester.h/.cpp` |
| **Reviewer** | A second model call that judges the result against the request. | `AiHarness.cpp` (`Review`) |
| **User** | The person at AutoCAD. Has the final word. | the `Install?` / `Run?` prompts |

## Responsibility matrix

R = responsible (does it), C = consulted (supplies input), - = not involved.

| Responsibility | Harness | Task | Model | Connector | Sandbox | Tester | Reviewer | User |
|---|---|---|---|---|---|---|---|---|
| State the request | - | C | - | - | - | - | - | R |
| Build the first context (prompt, API, current source, last error, history) | - | R | - | - | - | - | - | - |
| Write the code | - | - | R | - | - | - | - | - |
| Send the request, auth, timeout, HTTP errors | C | - | - | R | - | - | - | - |
| Strip fences and escapes from the reply | R | - | - | - | - | - | - | - |
| Decide what "valid" means | - | R | - | - | - | - | - | - |
| Call validation on every reply | R | - | - | - | - | - | - | - |
| Contain execution (no io/os, caps, ESC, read-only `at`) | - | - | - | - | R | - | - | - |
| Decide how to test-run | - | R | - | - | - | - | - | - |
| Run unattended in a scratch drawing, answer prompts, report, render | - | - | - | - | C | R | - | - |
| Judge the result against the request | C | - | - | C | - | C | R | - |
| Turn errors and FAIL into a correction round | R | - | - | - | - | - | - | - |
| Stop after `max_attempts` | R | - | - | - | - | - | - | - |
| Present code, report, image, verdict | R | - | - | - | - | - | - | - |
| Approve or refuse | C | - | - | - | - | - | - | R |
| Install / run after approval, backup, reload | - | R | - | - | - | - | - | - |
| Provider choice, model, keys, harness settings | - | - | - | C | - | - | - | R (`ai_config.lua`, `ATAISET*`) |

## Responsibilities in detail

### Harness (`AiHarness`)

Responsible for:

1. **Every model reply is checked.** No reply reaches the user, the drawing or a file without
   passing the task's `validate`.
2. **The loop is bounded.** At most `harness.max_attempts` model calls per request, correction
   rounds included.
3. **Failures become feedback, not dead ends.** A validation error, a runtime error in the test
   run, or a FAIL review is sent back to the model with the reason and, for a FAIL, the full test
   report, as the next round.
4. **The best valid result is kept.** If a later round fails (HTTP error, invalid code), an
   earlier valid result is still presented, with a note.
5. **A result is presented honestly.** The code, the test run (inputs, output, report), the image
   path and the verdict are shown as they are, including "not reviewed" and the reason.
6. **The human gate is applied consistently.** A FAIL never gets a Yes default; with
   `block_on_fail` it is refused outright.
7. **Settings are respected.** `test_run = false` skips the test run, `review = false` skips the
   review, and the presentation says so.

Not responsible for: what the prompt says, what counts as valid, how code is installed or run.
Those belong to the task.

### Task (`ATAICMD`, `ATAILUA`)

Responsible for:

1. **Context.** The first message: instructions, the `at` API (generated from `kFns`, never
   hand-written), the current source and last error when changing a command, the history for
   `ATAILUA`.
2. **Validation rules.** `ATAICMD`: known `at.*` names, the file loads in a throwaway engine,
   defines exactly the requested name once, a valid parameter list, no clash with existing
   commands. `ATAILUA`: known `at.*` names and a successful compile.
3. **Test-run choice.** Which tester function to call, and where the image goes.
4. **What happens after approval.** `ATAICMD`: back up the old file to `history\`, write, reload,
   report a load failure. `ATAILUA`: run in the user's drawing, record the history.

Not responsible for: retries, the review, the approval wording.

### Model

Responsible only for writing text. Nothing it writes is trusted: the harness validates it, the
sandbox contains it, the tester runs it away from the user's drawing, the reviewer and the user
judge it. A model that ignores instructions can waste attempts but cannot bypass a check.

### Connector (`AITools` + `AiConfig`)

Responsible for:

1. Reading the active provider from `ai_config.lua` on every request (edits apply immediately).
2. The request format (`openai` or `gemini`), auth (`bearer`, `query`, `none`), limits and the
   reply timeout.
3. Keys from the registry or `key_env`; never from or into `ai_config.lua`.
4. Images only to providers with `vision = true`; others fail clearly so the reviewer can fall
   back to text.
5. Turning transport and HTTP problems into readable `Error: ...` messages.

### Sandbox (`LuaEngine`)

Responsible for:

1. Only the base/table/string/math libraries; no `io`, `os`, `package`, `debug`, `dofile`,
   `loadfile`; `load` is text-only.
2. A read-only `at` proxy: scripts cannot replace API functions.
3. Instruction caps and ESC, so a runaway loop ends. Both are sticky for the run: `pcall` and
   `xpcall` are replaced so they re-raise an abort (and memory errors) instead of catching it.
   `setmetatable` refuses `__gc`, because finalizers run with hooks off and could loop forever.
4. A memory cap of 256 MB per Lua state (`cappedAlloc`), so a script cannot exhaust AutoCAD's
   process. Allocations made by C++ bindings are not counted.
5. Mode rules: loading mode (file top level), read-only mode (MCP queries), test-run mode (no
   reactor-linked labels, no file export), scripted / auto-answered input.

### Tester (`CommandTester`)

Responsible for:

1. **Isolation.** Every test run happens in a fresh in-memory drawing; the user's drawing is
   never touched (verified: entity counts identical before and after).
2. **Unattended input.** Declared parameters get defaults or sample values; any other prompt gets
   a sample answer, logged as `(auto)`.
3. **Facts, not opinions.** The geometry report states what was drawn (types, counts, extents,
   coordinates) and mechanical defects (zero-length, retraced and duplicated segments). It never
   judges whether that matches the request.
4. **The image.** A plan view auto-fitted to the result, with origin, grid step and vertex marks.
5. **Saying when it could not test.** "Not test-run" or "only partly test-run", with the reason.

### Reviewer (AI review)

Responsible for one decision: does the result do what the request asks? It gets the request, the
code, the test report (with auto-answered inputs marked) and, for vision providers, the image,
and must answer PASS or FAIL on the first line with reasons. Anything else counts as "not
reviewed".

Not responsible for safety: by the time it runs, the code has already been validated and
contained. A wrong verdict costs a retry or a question to the user, never an unchecked install.

### User

Responsible for:

1. The request: what is asked is what gets judged. A shortened request gives a correct but
   incomplete result.
2. The final decision at `Install?` / `Run?`, especially when the review says FAIL or could not
   review.
3. The configuration: provider, model, keys, harness settings.

## Guarantees (invariants)

These hold for every AI request through the harness. A change that breaks one is a bug.

1. AI-written code never touches the user's drawing before the user's approval (or, for
   `ATAILUA`, before a non-FAIL review).
2. AI-written code never runs outside the sandbox.
3. Every installed command passed validation; a replaced command is in `LuaCommands\history`.
4. A FAIL verdict is never installed or run without an explicit Yes, and never with
   `block_on_fail = true`.
5. The number of model calls per request is bounded by `max_attempts` (plus one review call per
   valid attempt).
6. API keys are never written into `ai_config.lua`, test reports or prompts.

## When an outside agent is the harness (MCP)

With Claude Code (or another MCP client) driving AutoCAD, the client is the harness and runs its
own loop and permissions. ArqaTools is the environment, and keeps these responsibilities:

| Responsibility | Where |
|---|---|
| Only the current Windows user can connect | pipe DACL, `PIPE_REJECT_REMOTE_CLIENTS` |
| Never type into an active AutoCAD prompt | Lua runs only when the document is quiescent (`ATMCPRUN`) |
| Queries cannot change the drawing | `run_lua` is always read-only |
| Agent runs never wait for a human | scripted answers / named parameters; missing input is an error listing the prompts |
| Every change is undoable in one step | each run is one `ATMCPRUN` command |
| The agent can see what a command does | `test_command`: report + plan-view image, user's drawing untouched |

What the outside harness owns (not ArqaTools): its agent loop, which tools it calls, its own
permission prompts, and judging the results it gets back.
