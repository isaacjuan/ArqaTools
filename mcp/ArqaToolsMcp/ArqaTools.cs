using System.ComponentModel;
using System.Text;
using System.Text.Json;
using ModelContextProtocol;
using ModelContextProtocol.Server;

namespace ArqaToolsMcp;

[McpServerToolType]
public class ArqaTools(BridgeClient bridge)
{
    private static readonly TimeSpan QueryTimeout = TimeSpan.FromSeconds(15);
    private static readonly TimeSpan RunTimeout   = TimeSpan.FromSeconds(90);

    [McpServerTool(Name = "list_instances", ReadOnly = true),
     Description("Lists the AutoCAD processes (pids) where ArqaTools' MCP bridge is running (ATMCPSTART).")]
    public static string ListInstances()
    {
        var pids = BridgeClient.ListInstances();
        return pids.Count == 0
            ? "No AutoCAD instance is listening. In AutoCAD: load the ArqaTools MCP build and run ATMCPSTART."
            : "Listening AutoCAD pids: " + string.Join(", ", pids);
    }

    [McpServerTool(Name = "select_instance"),
     Description("Chooses which AutoCAD instance the other tools talk to. Only needed when list_instances shows several.")]
    public async Task<string> SelectInstance([Description("Process id from list_instances")] int pid)
    {
        await bridge.SelectAsync(pid);
        return $"Using AutoCAD pid {pid}.";
    }

    [McpServerTool(Name = "ping", ReadOnly = true),
     Description("Checks the connection: returns the plugin version, AutoCAD version (ACADVER), current drawing and pid.")]
    public async Task<string> Ping(CancellationToken ct)
    {
        var r = await Call("ping", "", QueryTimeout, ct);
        return r.ToString();
    }

    [McpServerTool(Name = "get_api", ReadOnly = true),
     Description("Returns the reference of ArqaTools' Lua 'at' API (signatures and docs) and which functions " +
                 "run_lua may use (read-only mode). Read this before writing Lua.")]
    public async Task<string> GetApi(CancellationToken ct)
    {
        var r = await Call("get_api", "", QueryTimeout, ct);
        var readOnly = r.GetProperty("readOnly").EnumerateArray().Select(e => e.GetString());
        return r.GetProperty("api").GetString()
             + "\nUsable in run_lua (read-only mode): " + string.Join(", ", readOnly) + "\n"
             + "Lua 5.4 with base/table/string/math only. Coordinates WCS, handles are strings, angles in degrees.";
    }

    [McpServerTool(Name = "list_commands", ReadOnly = true),
     Description("Lists the installed Lua commands (name, description, file, last runtime error) and files that failed to load.")]
    public async Task<string> ListCommands(CancellationToken ct)
    {
        var r = await Call("list_commands", "", QueryTimeout, ct);
        return JsonSerializer.Serialize(r, new JsonSerializerOptions { WriteIndented = true });
    }

    [McpServerTool(Name = "get_command_source", ReadOnly = true),
     Description("Returns the Lua source of an installed command (or of NAME.lua if it failed to load).")]
    public async Task<string> GetCommandSource(
        [Description("Command name, e.g. ATLUAHELLO")] string name, CancellationToken ct)
    {
        var r = await Call("get_command_source", name, QueryTimeout, ct);
        return r.GetProperty("source").GetString() ?? "";
    }

    [McpServerTool(Name = "run_lua", ReadOnly = true),
     Description("Runs a Lua snippet in AutoCAD in READ-ONLY mode: only query functions (see get_api) are allowed; " +
                 "anything that changes the drawing, asks the user for input or writes a file raises an error. " +
                 "Use print(...) to return data. Runs as an AutoCAD command, so it waits while the user is in another command.")]
    public async Task<string> RunLua(
        [Description("Lua 5.4 code using the global 'at' table, e.g. for _,h in ipairs(at.entities(\"LINE\")) do print(h) end")] string code,
        CancellationToken ct)
    {
        var r = await bridge.CallAsync("run_lua", code, RunTimeout, ct);
        var output = r.TryGetProperty("output", out var o) ? o.GetString() ?? "" : "";
        if (r.GetProperty("ok").GetBoolean())
            return output.Length > 0 ? output : "(ok, no output)";

        var sb = new StringBuilder();
        if (r.TryGetProperty("cancelled", out var c) && c.GetBoolean()) sb.Append("Cancelled by the user (ESC). ");
        sb.Append("Lua error: ").Append(r.TryGetProperty("error", out var e) ? e.GetString() : "unknown");
        if (output.Length > 0) sb.Append("\nOutput before the error:\n").Append(output);
        throw new McpException(sb.ToString());
    }

    [McpServerTool(Name = "run_command", Destructive = true),
     Description("Runs an installed Lua command (see list_commands) in AutoCAD WITHOUT prompting the user. " +
                 "Its input calls (at.getPoint, at.getInt, ...) are answered in order from 'answers'. " +
                 "Read the command with get_command_source first to know which inputs it asks for and in which order. " +
                 "Answer formats: getPoint [x,y,z]; getInt/getReal/getDistance a number; getString/getKeyword a string; " +
                 "getEntity a handle string; getSelection an array of handles; null = press Enter (use the prompt's default). " +
                 "If the command asks for more inputs than given, it stops with an error listing the prompts. " +
                 "The whole run is one UNDO step in AutoCAD. Only Lua commands can be run, not built-in AutoCAD commands.")]
    public async Task<string> RunCommand(
        [Description("Lua command name, e.g. ATGRID")] string name,
        [Description("Answers to the command's inputs, in the order it asks, e.g. [[0,0,0], 3, 4, 10]")] JsonElement[]? answers = null,
        CancellationToken ct = default)
    {
        if (name.Contains('\n')) throw new McpException("Invalid command name.");
        var body = name.Trim() + "\n" + LuaLiteral.FromAnswers(answers ?? []);
        var r = await bridge.CallAsync("run_command", body, RunTimeout, ct);
        var output = r.TryGetProperty("output", out var o) ? o.GetString() ?? "" : "";
        if (r.GetProperty("ok").GetBoolean())
            return output.Length > 0 ? output : $"{name} finished (no output).";

        var sb = new StringBuilder();
        if (r.TryGetProperty("cancelled", out var c) && c.GetBoolean()) sb.Append("Cancelled by the user (ESC). ");
        sb.Append(name).Append(" failed: ").Append(r.TryGetProperty("error", out var e) ? e.GetString() : "unknown");
        if (output.Length > 0) sb.Append("\nOutput before the error:\n").Append(output);
        throw new McpException(sb.ToString());
    }

    // Calls the bridge; a response with ok=false becomes a tool error.
    private async Task<JsonElement> Call(string method, string body, TimeSpan timeout, CancellationToken ct)
    {
        var r = await bridge.CallAsync(method, body, timeout, ct);
        if (!r.GetProperty("ok").GetBoolean())
            throw new McpException(r.TryGetProperty("error", out var e) ? e.GetString() ?? "error" : "error");
        return r;
    }
}
