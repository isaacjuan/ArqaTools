# AI Setup (ArqaTools)

ArqaTools' AI commands talk to one configurable, OpenAI-style chat endpoint. Pick a
provider, store its API key, and every AI command uses it.

## Quick start

1. Load `ArqaTools.arx` in AutoCAD.
2. `ATAISETENDPOINT` - choose a provider (see the table below).
3. `ATAISETTOKEN` - paste that provider's API key (skip this for Ollama).
4. `ATAISETMODEL` - set a model, if the provider uses one (see the table).
5. `ATAITEST` - send a test message and check the reply.

## Providers

`ATAISETENDPOINT` offers:

| # | Endpoint | Key | Model used |
|---|---|---|---|
| 1 | `models.inference.ai.azure.com` (GitHub Models, about 50 requests/day) | GitHub token | `ATAISETMODEL` value (default `gpt-4o`) |
| 2 | `api.githubcopilot.com` (Copilot subscription) | GitHub token | `ATAISETMODEL` value (default `gpt-4o`) |
| 3 | `generativelanguage.googleapis.com` (Google Gemini, free tier) | key from https://aistudio.google.com/app/apikey | always `gemini-2.5-flash` |
| 4 | `api.openai.com` | OpenAI key | `ATAISETMODEL` value (default `gpt-4o`) |
| 5 | `api.deepseek.com` | key from https://platform.deepseek.com/api_keys | always `deepseek-chat` |
| 6 | `localhost:11434` (Ollama, local) | none | `ATAISETMODEL` value, e.g. `llama3.2`, `mistral` |
| 7 | Custom host (OpenAI-compatible, `/chat/completions`) | as required | `ATAISETMODEL` value |

Notes:

- The default, when nothing has been configured, is option 1 with `gpt-4o`.
- For Gemini and DeepSeek the model is fixed in code; `ATAISETMODEL` has no effect.
- Ollama must be running (`ollama serve`). Plain HTTP is used for `localhost`/`127.0.0.1`,
  HTTPS for everything else. `ATAILISTMODELS` lists the installed Ollama (or Gemini) models.
- Local models usually write weaker Lua than the cloud ones. `ATAICMD` validates and retries,
  which helps, but expect more corrections.

## Where settings are stored

`HKEY_CURRENT_USER\Software\ArqaToolsPlugin`:

| Value | Holds |
|---|---|
| `APIEndpoint` | the chosen endpoint host |
| `GitHubToken` | the API key for that endpoint (the name is historical: it holds a DeepSeek, OpenAI or Gemini key just the same) |
| `AIModel` | the model name; if the value exists but is empty, OpenAI-style providers get an empty model and fail, so set one with `ATAISETMODEL` |

To see the current setup without exposing the key:

```powershell
Get-ItemProperty HKCU:\Software\ArqaToolsPlugin | Select-Object APIEndpoint, AIModel
```

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
| `ATAISETTOKEN`, `ATAISETENDPOINT`, `ATAISETMODEL`, `ATAITEST`, `ATAILISTMODELS` | Configuration |

`ATAICMD` does not use the shared history: each request sends the command's current source and
its last load or run error. The reply is validated in a sandbox and retried up to twice, then
shown to you for approval before it is installed. See `AGENTS.md` (`LuaCommands`) for details.

## Troubleshooting

Some error messages still use the old command names without the `AT` prefix: `AISETTOKEN` there
means `ATAISETTOKEN`.

| Message | Meaning / fix |
|---|---|
| `API token not configured` | No key stored and the endpoint is not Ollama. Run `ATAISETTOKEN`. |
| `HTTP 401` | Key invalid, expired, or for a different provider than the endpoint. |
| `HTTP 403` | The account has no access to that service (e.g. no Copilot subscription). |
| `HTTP 429` | Rate limit (GitHub Models about 50/day, Gemini free tier about 1500/day). Wait, or switch provider. |
| `WinHTTP error 12007` | Host name not resolved: no internet or DNS problem. |
| `WinHTTP error 12029` | Cannot connect: host unreachable or blocked by a firewall. For Ollama, check `ollama serve`. |
| `WinHTTP error 12002` | Timeout. |
| `WinHTTP error 12157` / `12175` | TLS failed, often a corporate proxy or firewall doing SSL inspection with a certificate Windows does not trust. |
| Empty or garbled reply | The model returned nothing usable; retry, or try a stronger model. |

## Security notes

- The key is stored in plain text in `HKEY_CURRENT_USER` (per user, protected only by Windows
  account security). Never paste it into chats, tickets or source files.
- To revoke a key, use the provider's own key page, then store a new one with `ATAISETTOKEN`.
- AI-written Lua runs in a sandbox: no file, OS or network access, except `at.exportSvg`, which
  can only write a plain file name into Documents. `ATAICMD` never installs anything without
  your confirmation, and keeps previous versions in `LuaCommands\history`.
