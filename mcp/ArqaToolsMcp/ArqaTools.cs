using System.ComponentModel;
using System.Text;
using System.Text.Json;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;
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
     Description("Prefer the command's own tool: Lua commands that declare parameters are published as tools " +
                 "named after the command (e.g. ATGRID) with named, typed arguments. Use run_command for commands without " +
                 "declared parameters. " +
                 "Runs an installed Lua command (see list_commands) in AutoCAD WITHOUT prompting the user. " +
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

    [McpServerTool(Name = "run_acad_command", Destructive = true),
     Description("Runs ANY AutoCAD or AutoCAD Architecture command in the open drawing, as if typed at the command " +
                 "line (e.g. _WALLADD, _DOORADD, _SPACEADD, _LINE, _-LAYER). 'inputs' are the answers to its prompts, " +
                 "in order: a string is typed as is (keywords, names, \"10,20\"), \"\" or null = Enter, a number is a " +
                 "value, [x, y, z] is a point in WCS, {\"handle\": \"2A\"} picks one entity (\"Select object:\"), " +
                 "{\"handles\": [\"2A\", \"2B\"]} is a selection set (\"Select objects:\"; follow it with \"\" to end the " +
                 "selection). Results a command only prints (AREA, DIST, LIST...) are not returned: read them " +
                 "afterwards with run_lua, e.g. print(at.getVar(\"AREA\"), at.getVar(\"PERIMETER\")). Use the English _NAME " +
                 "form and the command-line version of commands that open dialogs (e.g. _-LAYER, _-INSERT). The " +
                 "command must finish with the inputs given (end with \"\" where it waits for more), otherwise " +
                 "AutoCAD cancels it; the result lists the entities created (handle, class, layer) and the last " +
                 "prompt, which shows where it stopped. A command that loops on a prompt (e.g. \"Select objects:\") " +
                 "shows that prompt last even when it finished; check the drawing or at.getVar to be sure. One UNDO step. Use list_acad_commands to discover commands. " +
                 "Prefer an ArqaTools command tool when one does the job.")]
    public async Task<string> RunAcadCommand(
        [Description("Command name, e.g. _WALLADD")] string command,
        [Description("Answers to the command's prompts in order, e.g. [[0,0,0], [5000,0,0], \"\"]")] JsonElement[]? inputs = null,
        CancellationToken ct = default)
    {
        command = command.Trim();
        if (command.Length == 0 || command.Length > 64 ||
            command.Any(ch => !char.IsLetterOrDigit(ch) && ch != '_' && ch != '-' && ch != '.' && ch != '+'))
            throw new McpException("Invalid command name.");
        var body = command + "\n" + LuaLiteral.FromAnswers(inputs ?? []);
        var r = await bridge.CallAsync("acad_command", body, RunTimeout, ct);
        var output = r.TryGetProperty("output", out var o) ? o.GetString() ?? "" : "";
        if (r.GetProperty("ok").GetBoolean())
            return output.Length > 0 ? output : $"{command} finished.";

        var sb = new StringBuilder();
        if (r.TryGetProperty("cancelled", out var c) && c.GetBoolean()) sb.Append("Cancelled by the user (ESC). ");
        sb.Append(command).Append(" failed: ").Append(r.TryGetProperty("error", out var e) ? e.GetString() : "unknown");
        if (output.Length > 0) sb.Append('\n').Append(output);
        throw new McpException(sb.ToString());
    }

    [McpServerTool(Name = "list_acad_commands", ReadOnly = true),
     Description("Lists the AutoCAD commands registered by modules (AutoCAD Architecture, MEP, ArqaTools, other " +
                 "plug-ins), grouped by command group, optionally filtered by a name fragment (e.g. WALL, DOOR, " +
                 "SPACE). Core AutoCAD commands (LINE, CIRCLE, MOVE...) are not listed but run_acad_command runs them too.")]
    public async Task<string> ListAcadCommands(
        [Description("Optional part of the command name, e.g. WALL")] string? filter = null,
        CancellationToken ct = default)
    {
        var r = await Call("list_acad_commands", filter ?? "", QueryTimeout, ct);
        return JsonSerializer.Serialize(r, new JsonSerializerOptions { WriteIndented = true });
    }

    [McpServerTool(Name = "test_command", ReadOnly = true),
     Description("Test-runs an installed Lua command in a scratch drawing (the user's drawing is not touched), " +
                 "using each declared parameter's default or a plausible test value, and returns what it printed, " +
                 "a geometry report (entities, extents, defects such as zero-length or doubled-back segments) and a " +
                 "plan-view image of what it drew. Use it to check that a command does what it should.")]
    public async Task<IEnumerable<ContentBlock>> TestCommand(
        [Description("Lua command name, e.g. ATSTAIR")] string name, CancellationToken ct)
    {
        var r = await Call("test_command", name, RunTimeout, ct);
        string S(string key) => r.TryGetProperty(key, out var v) ? v.GetString() ?? "" : "";

        var sb = new StringBuilder();
        if (!r.GetProperty("ran").GetBoolean())
            sb.Append("Not test-run: ").Append(S("skipped"));
        else
        {
            sb.Append("Parameters used: ").Append(S("params")).Append('\n');
            if (S("skipped").Length > 0) sb.Append("Note: ").Append(S("skipped")).Append('\n');
            if (S("output").Length > 0) sb.Append("Printed output:\n").Append(S("output"));
            if (!r.GetProperty("runOk").GetBoolean()) sb.Append("It stopped with an error:\n").Append(S("error")).Append('\n');
            sb.Append(S("report"));
        }
        var blocks = new List<ContentBlock> { new TextContentBlock { Text = sb.ToString() } };
        var png = S("png");
        if (png.Length > 0 && File.Exists(png))
        {
            blocks.Add(ImageContentBlock.FromBytes(await File.ReadAllBytesAsync(png, ct), "image/png"));
            blocks.Add(new TextContentBlock { Text = "Plan view (+X right, +Y up; red cross = origin = test base point; " +
                                                     "blue dots = vertices) saved at " + png });
        }
        return blocks;
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
