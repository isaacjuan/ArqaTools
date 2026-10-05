using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;

namespace ArqaToolsMcp;

/// <summary>
/// Every Lua command that declares a parameter list (at.defineCommand's 4th
/// argument) is published as its own MCP tool, with a JSON schema built from
/// that list. The set follows AutoCAD live: a poller notifies the client
/// (tools/list_changed) when commands are added, changed or removed.
/// </summary>
public sealed class CommandTools(BridgeClient bridge, ILogger<CommandTools> log)
{
    private static readonly TimeSpan ListTimeout = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan RunTimeout  = TimeSpan.FromSeconds(90);
    private static readonly TimeSpan PollEvery   = TimeSpan.FromSeconds(5);

    private McpServer? _server;
    private string _listedSignature = "";
    private int _pollerStarted;

    public async ValueTask<ListToolsResult> ListAsync(RequestContext<ListToolsRequestParams> ctx, CancellationToken ct)
    {
        _server ??= ctx.Server;
        StartPoller();
        var commands = await FetchAsync(ct);
        _listedSignature = Signature(commands);
        return new ListToolsResult { Tools = commands.Select(ToTool).ToList() };
    }

    public async ValueTask<CallToolResult> CallAsync(RequestContext<CallToolRequestParams> ctx, CancellationToken ct)
    {
        var name = ctx.Params?.Name ?? "";
        if (name.Length == 0 || name.Any(ch => !char.IsLetterOrDigit(ch) && ch != '_'))
            return Error($"Unknown tool '{name}'.");

        var args = ctx.Params?.Arguments ?? new Dictionary<string, JsonElement>();
        JsonElement response;
        try
        {
            response = await bridge.CallAsync("call_command", name + "\n" + LuaLiteral.FromObject(args), RunTimeout, ct);
        }
        catch (McpException ex)
        {
            return Error(ex.Message);
        }

        var output = response.TryGetProperty("output", out var o) ? o.GetString() ?? "" : "";
        if (response.GetProperty("ok").GetBoolean())
            return Text(output.Length > 0 ? output : $"{name} finished (no output).");

        var sb = new StringBuilder();
        if (response.TryGetProperty("cancelled", out var c) && c.GetBoolean()) sb.Append("Cancelled by the user (ESC). ");
        sb.Append(name).Append(" failed: ").Append(response.TryGetProperty("error", out var e) ? e.GetString() : "unknown");
        if (output.Length > 0) sb.Append("\nOutput before the error:\n").Append(output);
        return Error(sb.ToString());
    }

    // Commands with declared parameters; empty when AutoCAD is not reachable.
    private async Task<List<JsonElement>> FetchAsync(CancellationToken ct)
    {
        if (BridgeClient.ListInstances().Count == 0) return [];
        try
        {
            var r = await bridge.CallAsync("list_commands", "", ListTimeout, ct);
            return r.GetProperty("commands").EnumerateArray()
                    .Where(c => c.TryGetProperty("params", out var p) && p.ValueKind == JsonValueKind.Array)
                    .ToList();
        }
        catch (Exception ex) when (ex is McpException or OperationCanceledException)
        {
            log.LogDebug("list_commands failed: {Message}", ex.Message);
            return [];
        }
    }

    private static string Signature(List<JsonElement> commands) =>
        string.Join("\n", commands.Select(c =>
            c.GetProperty("name").GetString() + "|" + c.GetProperty("description").GetString() + "|" +
            c.GetProperty("params").GetRawText()));

    private void StartPoller()
    {
        if (Interlocked.Exchange(ref _pollerStarted, 1) == 1) return;
        _ = Task.Run(async () =>
        {
            while (true)
            {
                await Task.Delay(PollEvery);
                try
                {
                    var signature = Signature(await FetchAsync(CancellationToken.None));
                    if (signature == _listedSignature || _server is null) continue;
                    _listedSignature = signature;   // the client re-lists after the notification
                    await _server.SendNotificationAsync(NotificationMethods.ToolListChangedNotification, CancellationToken.None);
                }
                catch (Exception ex)
                {
                    log.LogDebug("tool poll failed: {Message}", ex.Message);
                }
            }
        });
    }

    private static Tool ToTool(JsonElement command)
    {
        var name = command.GetProperty("name").GetString()!;
        var properties = new JsonObject();
        var required = new JsonArray();
        foreach (var p in command.GetProperty("params").EnumerateArray())
        {
            var pname = p.GetProperty("name").GetString()!;
            var schema = SchemaFor(p);
            properties[pname] = schema;
            bool hasDefault = p.TryGetProperty("default", out _);
            bool optional = p.TryGetProperty("optional", out var opt) && opt.GetBoolean();
            if (!hasDefault && !optional) required.Add(pname);
        }
        var input = new JsonObject { ["type"] = "object", ["properties"] = properties, ["additionalProperties"] = false };
        if (required.Count > 0) input["required"] = required;

        var description = command.GetProperty("description").GetString() ?? "";
        return new Tool
        {
            Name = name,
            Description = (description.Length > 0 ? description + ". " : "") +
                          $"ArqaTools Lua command {name}, run in the open AutoCAD drawing as one UNDO step.",
            InputSchema = JsonSerializer.SerializeToElement(input),
            Annotations = new ToolAnnotations { DestructiveHint = true, ReadOnlyHint = false },
        };
    }

    private static JsonObject SchemaFor(JsonElement p)
    {
        var type = p.GetProperty("type").GetString();
        JsonObject s = type switch
        {
            "point"     => new() { ["type"] = "array", ["items"] = new JsonObject { ["type"] = "number" },
                                   ["minItems"] = 2, ["maxItems"] = 3 },
            "integer"   => new() { ["type"] = "integer" },
            "number" or "distance" => new() { ["type"] = "number" },
            "keyword"   => new() { ["type"] = "string",
                                   ["enum"] = JsonNode.Parse(p.GetProperty("options").GetRawText()) },
            "selection" => new() { ["type"] = "array", ["items"] = new JsonObject { ["type"] = "string" } },
            _           => new() { ["type"] = "string" },
        };

        var text = new StringBuilder();
        if (p.TryGetProperty("description", out var d)) text.Append(d.GetString());
        else if (p.TryGetProperty("prompt", out var pr)) text.Append(pr.GetString());
        if (type == "point")     text.Append(" [x, y, z] in WCS (z optional)");
        if (type == "entity")    text.Append(" (entity handle)");
        if (type == "selection") text.Append(" (entity handles");
        if (type == "selection" && p.TryGetProperty("filter", out var f)) text.Append(", types ").Append(f.GetString());
        if (type == "selection") text.Append(')');
        if (text.Length > 0) s["description"] = text.ToString().Trim();
        if (p.TryGetProperty("default", out var def)) s["default"] = JsonNode.Parse(def.GetRawText());
        return s;
    }

    private static CallToolResult Text(string text) =>
        new() { Content = [new TextContentBlock { Text = text }] };

    private static CallToolResult Error(string text) =>
        new() { Content = [new TextContentBlock { Text = text }], IsError = true };
}
