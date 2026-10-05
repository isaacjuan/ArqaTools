using ArqaToolsMcp;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ModelContextProtocol.Protocol;

var builder = Host.CreateApplicationBuilder(args);

// stdout carries the MCP protocol; all logging goes to stderr (and not to the
// Windows event log the default host would add).
builder.Logging.ClearProviders();
builder.Logging.AddConsole(o => o.LogToStandardErrorThreshold = LogLevel.Trace);

builder.Services.AddSingleton<BridgeClient>();
builder.Services.AddSingleton<CommandTools>();
builder.Services
    .AddMcpServer(o => o.Capabilities = new ServerCapabilities { Tools = new ToolsCapability { ListChanged = true } })
    .WithStdioServerTransport()
    .WithToolsFromAssembly()
    // Lua commands that declare parameters, one tool each, next to the static tools.
    .WithListToolsHandler((ctx, ct) => ctx.Services!.GetRequiredService<CommandTools>().ListAsync(ctx, ct))
    .WithCallToolHandler((ctx, ct) => ctx.Services!.GetRequiredService<CommandTools>().CallAsync(ctx, ct));

await builder.Build().RunAsync();
