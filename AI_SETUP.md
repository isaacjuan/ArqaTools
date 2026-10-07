# AI Setup (ArqaTools)

ArqaTools' AI commands talk to one active provider. Providers are defined in an external
Lua file, `Documents\ArqaTools\ai_config.lua`; API keys are stored separately, per provider.

## Quick start

1. Load `ArqaTools.arx` in AutoCAD.
2. `ATAISETENDPOINT` - choose a provider from the file (number or name).
3. `ATAISETTOKEN` - paste that provider's API key (skip this for Ollama).
4. `ATAISETMODEL` - change the model, if needed.
5. `ATAITEST` - send a test message and check the reply.

## ai_config.lua

Written with defaults the first time an AI command runs. `ATAICONFIG` opens it in Notepad.
It is read on every AI request: edit, save, and the next `ATAI*` / `ATAICMD` call uses it
(no reload, no rebuild). It runs in a sandbox (no file or OS access) and only data is read.

```lua
active = "deepseek"            -- the provider the AI commands use

providers = {
  deepseek = {
    label = "DeepSeek",
    url = "https://api.deepseek.com/chat/completions",
    format = "openai", auth = "bearer", model = "deepseek-flash", vision = true,
    key_url = "https://platform.deepseek.com/api_keys",
    models_url = "https://api.deepseek.com/models",
  },
  -- ...
}
```

| Field | Meaning |
|---|---|
| `label` | Shown by `ATAISETENDPOINT` |
| `url` | Request URL; `{model}` is replaced by the model (used by Gemini) |
| `format` | `"openai"` (chat/completions JSON), `"gemini"` (generateContent) or `"anthropic"` (Claude Messages API) |
| `auth` | `"bearer"` (Authorization header), `"x-api-key"` (Anthropic), `"query"` (`?key=`, Gemini) or `"none"` (Ollama) |
| `model` | Model sent with each request (`ATAISETMODEL` rewrites it) |
| `vision` | `true` if the model reads images: `ATAICMD`'s test-run review then attaches a plan view of what the new command drew; `false` falls back to the text report |
| `max_tokens`, `temperature` | Request limits (defaults 8192 and 0.7). `anthropic` sends `temperature` only when it is set in the file: Claude 4.7 and later reject any non-default value with HTTP 400 |
| `timeout` | Seconds to wait for the reply (default 180). WinHTTP's own default is 30 s, too short for models that reason before answering (DeepSeek's default thinking mode) |
| `key_env` | Optional: environment variable holding the key (wins over the stored key) |
| `key_url` | Where to get a key (shown by `ATAISETENDPOINT` / `ATAISETTOKEN`) |
| `models_url` | Optional: URL that lists the models (`ATAILISTMODELS`) |
| `extra` | Optional: raw JSON members added to every request body, e.g. `'"thinking":{"type":"disabled"}'` |

Default providers: `github` (GitHub Models), `copilot`, `gemini`, `openai`, `anthropic`, `deepseek`
(`deepseek-flash` reads images, `deepseek-v4-pro` is text-only), `ollama` (local, no key).
Any other OpenAI-compatible service is one more entry in `providers`.

`anthropic` (Claude) is in the defaults of a newly written file. An existing `ai_config.lua` is
never rewritten, so add it by hand:

```lua
  anthropic = {
    label = "Anthropic Claude",
    url = "https://api.anthropic.com/v1/messages",
    format = "anthropic", auth = "x-api-key", model = "claude-sonnet-5-5", vision = true,
    key_url = "https://platform.claude.com/settings/keys",
    models_url = "https://api.anthropic.com/v1/models",
  },
```

A reply cut off at `max_tokens` (`anthropic`: `stop_reason` `max_tokens`; `openai`:
`finish_reason` `length`), or refused by Claude, is reported as an error instead of being
treated as code.

`ATAISETENDPOINT` and `ATAISETMODEL` rewrite the `active` line and the provider's `model`
value in place; everything else in the file, comments included, is kept.

### harness

An optional `harness = { max_attempts, test_run, review, block_on_fail }` table sets how
`ATAICMD` and `ATAILUA` check AI-written code before you accept it. See `HARNESS.md`.

### First run (migration)

The file is created from the old registry settings: the old endpoint becomes `active`
(an unknown host becomes a `custom` provider), the old model is kept where it applies,
and the old single key is stored for that provider.

## Where keys are stored

Never in `ai_config.lua` (so the file can be shared). `ATAISETTOKEN` stores the key for the
active provider in `HKEY_CURRENT_USER\Software\ArqaToolsPlugin\Keys`, one value per provider
name. A provider with `key_env` uses that environment variable instead when it is set.

The old values in `HKEY_CURRENT_USER\Software\ArqaToolsPlugin` (`APIEndpoint`, `AIModel`,
`GitHubToken`) are only read once, for the migration above.

## Commands that use the AI

| Command | What it does |
|---|---|
| `ATAIASK` | Ask a question; the answer prints on the command line |
| `ATAIDRAW` | Draw from a natural-language description |
| `ATAILISP` | Generate AutoLISP (copied to the clipboard to paste) |
| `ATAIFIX` | Describe an error and get a fix |
| `ATAIHELP` | Built-in knowledge base about ArqaTools commands |
| `ATAILUA` | AI writes a Lua script against the `at` API and runs it immediately |
| `ATAICMD` | AI creates or changes a Lua command, saved in `Documents\ArqaTools\LuaCommands` |
| `ATAICLEAR` | Clear the conversation history (shared by `ATAILISP`/`ATAIFIX`/`ATAILUA`; `ATAIASK` sends each question on its own) |
| `ATAICONFIG`, `ATAISETENDPOINT`, `ATAISETMODEL`, `ATAISETTOKEN`, `ATAITEST`, `ATAILISTMODELS` | Configuration |

`ATAICMD` does not use the shared history: each request sends the command's current source and
its last load or run error. The reply is validated in a sandbox, test-run in a scratch drawing,
reviewed against your request (with a plan view image when the provider has `vision = true`),
retried up to twice, then shown to you for approval before it is installed. See `AGENTS.md`
(`LuaCommands`) for details.

## Troubleshooting

| Message | Meaning / fix |
|---|---|
| `AI configuration error: ...ai_config.lua:N: ...` | The file has a Lua error at line N. `ATAICONFIG` opens it. |
| `no API key for provider 'x'` | Run `ATAISETTOKEN` (or set `key_env`). |
| `images not supported by x` | That provider has `vision = false`; `ATAICMD` reviews the text report only. |
| `HTTP 401` | Key invalid, expired, or for a different provider. |
| `HTTP 403` | The account has no access to that service (e.g. no Copilot subscription). |
| `HTTP 404` | Wrong `url` or `model` for that provider. |
| `HTTP 429` | Rate limit. Wait, or switch provider. |
| `WinHTTP error 12007` | Host name not resolved: no internet or DNS problem. |
| `WinHTTP error 12029` | Cannot connect: host unreachable or blocked by a firewall. For Ollama, check `ollama serve`. |
| `WinHTTP error 12002` | Timeout: no reply within `timeout` seconds. Raise it for that provider, or use a faster model. |
| `WinHTTP error 12157` / `12175` | TLS failed, often a corporate proxy or firewall doing SSL inspection with a certificate Windows does not trust. |
| Empty or garbled reply | The model returned nothing usable; retry, or try a stronger model. |

## Security notes

- Keys are stored in plain text in `HKEY_CURRENT_USER` (per user, protected only by Windows
  account security). Never paste them into chats, tickets or source files, or into `ai_config.lua`.
- To revoke a key, use the provider's own key page, then store a new one with `ATAISETTOKEN`.
- AI-written Lua runs in a sandbox: no file, OS or network access, except `at.exportSvg`, which
  can only write a plain file name into Documents. `ATAICMD` never installs anything without
  your confirmation, and keeps previous versions in `LuaCommands\history`.
