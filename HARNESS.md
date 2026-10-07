# Harness in ArqaTools

In AI-agent terms, a *harness* is everything around a language model that turns its raw text
output into reliable action: the context it gets, the tools it can use, the sandbox, the checks,
the retries and the human approval. The model only writes text; the harness decides what happens
with it.

The word fits three parts of ArqaTools, in three different roles. Who is responsible for what,
and the guarantees the pipeline gives, are in `HARNESS_RESPONSIBILITIES.md`.

| Part | Role | Who is the model |
|---|---|---|
| `AiHarness` (used by `ATAICMD`, `ATAILUA`) | **Agent harness**: ArqaTools wraps a model | The provider in `ai_config.lua` (e.g. DeepSeek) |
| MCP bridge + `ArqaToolsMcp.exe` | **Environment** for an outside harness (Claude Code) | Claude, driven by Claude Code |
| `CommandTester` | **Test harness** (testing sense) | none: it runs code, not a model |

## 1. AiHarness: ArqaTools as an agent harness

When you run `ATAICMD` or `ATAILUA`, the model only produces Lua text. `AiHarness`
(`AiHarness.h/.cpp`) turns it into code that is safe to accept:

```
context  ->  model call  ->  cleanup  ->  validation  ->  test run  ->  review
   ^                                          |              |            |
   +------------- correction round <----------+--------------+-- FAIL ----+

then: present (code, report, plan view, verdict)  ->  human gate (Install? / Run?)
```

| Harness part | In ArqaTools |
|---|---|
| Model connection | `AITools` + `ai_config.lua` (provider, model, limits, timeout) - see `AI_SETUP.md` |
| Context | The task's first message: `BuildPrompt` for `ATAICMD` (request, current source, last error, the `at` API generated from `kFns`); the instruction prompt plus conversation history for `ATAILUA` |
| Output parsing | `LuaTools::CleanAiLuaResponse` |
| Action space | The `at.*` API and, for commands, the declared parameters |
| Sandbox | `LuaEngine`: no io/os/package/debug, read-only `at`, instruction cap, ESC |
| Validation (per task) | `ATAICMD`: `ValidateFile` (API names, trial load, exactly one definition, parameter list). `ATAILUA`: API names + compile check |
| Test run (per task) | `CommandTester::Run` (command) / `CommandTester::RunScript` (script): scratch drawing, geometry report, plan-view PNG |
| Evaluation | AI review: a second model call gets the request, code, report and image, answers PASS / FAIL (LLM as judge) |
| Feedback loop | A validation error or a FAIL goes back to the model as a correction round, up to `max_attempts` |
| Human gate | `AiHarness::Approve`: `ATAICMD` always asks "Install?"; `ATAILUA` runs at once unless the review failed. History and rollback in `LuaCommands\history` |

### What a task supplies

`AiHarness::Task` holds only what differs per use:

- `label`, `request`: shown to the user and judged by the reviewer;
- `messages`: the context for the first model call (the harness appends correction rounds);
- `validate(code, err)`: required;
- `testRun(code)`: optional.

Everything else (model call, cleanup, retries, the review with the image, the wording of the
approval) is shared. A new AI feature reuses the harness by filling in a `Task`.

### Test runs without a user

A test run happens in an in-memory scratch drawing, so the user's drawing is never touched.
Code that asks for input still runs unattended: once the declared parameters (or none, for a
script) are used up, every `at.get*` gets a sample answer (`LuaRunOptions::autoAnswer`):

| Input | Sample answer |
|---|---|
| `at.getPoint` | (0,0), (10,0), (10,10), (0,10), then the same 20 units further right |
| `at.getDistance` | 10 |
| `at.getReal` / `at.getInt` / `at.getString` | the prompt's default, else 10 / 3 / "Test" |
| `at.getKeyword` | the default, else the first keyword |
| `at.getEntity` / `at.getSelection` | nothing (an empty drawing has no objects) |

The report lists every answered input marked `(auto)`, and the reviewer is told these were
sample values. Functions that would outlive the scratch drawing (reactor-linked labels) or write
files are blocked during a test run, and the run is reported as only partly tested.

### Settings

The optional `harness` table in `ai_config.lua` (defaults shown):

```lua
harness = {
  max_attempts  = 3,      -- model calls per request, correction rounds included
  test_run      = true,   -- run the result once in a scratch drawing and report what it drew
  review        = true,   -- a second AI call judges the result (and its plan view) against the request
  block_on_fail = false,  -- true: never install or run code the review failed
}
```

New `ai_config.lua` files get this block; an older file without it uses the defaults. Copy the
block above into it to change them.

### Limits

- The review is a model judging a model: it can be wrong in both directions. That is why a FAIL
  only changes the question's default to No (unless `block_on_fail`), and you have the last word.
- A PASS checks the result against the request as written. A vague or cut-off request gives a
  correct-but-incomplete command (seen with ATSTAIR when the prompt was shortened).
- The test run uses one set of sample values; it does not prove the code right for every input.
- Only providers with `vision = true` see the plan view; the others review the text report.

## 2. MCP: ArqaTools as the environment for an outside harness

When an MCP client such as Claude Code drives AutoCAD, **that client is the harness**: it runs the
agent loop, the permissions and the context. ArqaTools is the *environment*, and the MCP bridge
(`McpBridge.cpp`) plus `ArqaToolsMcp.exe` are its interface:

| Environment part | In ArqaTools |
|---|---|
| Actions | MCP tools: `run_command`, `run_lua`, and one tool per Lua command with declared parameters |
| Observations | `ping`, `list_commands`, `get_command_source`, `get_api`, read-only `run_lua`, and `test_command`, whose plan-view image is the agent's eyes |
| Safety rules the harness relies on | read-only mode for `run_lua`; no prompting during agent runs; Lua only runs when no other AutoCAD command is active; one UNDO step per run; the pipe accepts only the current Windows user |

So ArqaTools is not the harness here, but it supplies the guard rails the harness depends on.

## 3. CommandTester: a test harness

In the testing sense, a test harness runs a unit in a controlled setup with fixed inputs and
collects the results. `CommandTester` does exactly that for one command or script: an empty
scratch drawing, fixed sample inputs, and the results as a geometry report (counts, extents,
zero-length / retraced / duplicated segments) and a plan-view PNG. It is used by step 1's test
run and by the `test_command` MCP tool.

## Next step

An MCP tool `create_command(name, request)` would become a thin wrapper around `AiHarness`: an
agent's request goes through the same validation, test run and review as `ATAICMD`, and you
still approve the install in AutoCAD.
