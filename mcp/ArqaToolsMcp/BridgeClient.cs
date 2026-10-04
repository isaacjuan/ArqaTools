using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using ModelContextProtocol;

namespace ArqaToolsMcp;

/// <summary>
/// Client for the pipe that ArqaTools.arx opens after ATMCPSTART
/// (\\.\pipe\ArqaTools.&lt;pid&gt;, see McpBridge.h).
///
/// Frames: 4-byte little-endian length + UTF-8. Request "method\nbody",
/// response one JSON object with "ok" and, on failure, "error".
/// One request in flight at a time, one connection per call (the plugin
/// accepts a single connection, so holding it would lock out other clients).
/// </summary>
public sealed class BridgeClient : IDisposable
{
    private const string PipePrefix = "ArqaTools.";
    // ConnectAsync waits while another client's call holds the pipe, so this
    // also bounds how long we queue behind it.
    private const int ConnectTimeoutMs = 30_000;
    private const int MaxFrame = 16 * 1024 * 1024;

    private readonly SemaphoreSlim _lock = new(1, 1);
    private NamedPipeClientStream? _pipe;
    private int? _selectedPid;

    /// <summary>Process ids of AutoCAD instances with the bridge running.</summary>
    public static List<int> ListInstances()
    {
        var pids = new List<int>();
        foreach (var path in Directory.GetFiles(@"\\.\pipe\"))
        {
            var name = Path.GetFileName(path);
            if (name.StartsWith(PipePrefix, StringComparison.OrdinalIgnoreCase)
                && int.TryParse(name.AsSpan(PipePrefix.Length), out int pid))
                pids.Add(pid);
        }
        return pids;
    }

    public async Task SelectAsync(int pid)
    {
        await _lock.WaitAsync();
        try
        {
            _selectedPid = pid;
            Disconnect();
        }
        finally { _lock.Release(); }
    }

    /// <summary>Sends a request and returns the parsed response. Throws on transport errors.</summary>
    public async Task<JsonElement> CallAsync(string method, string body, TimeSpan timeout, CancellationToken ct)
    {
        using var timeoutCts = new CancellationTokenSource(timeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct, timeoutCts.Token);

        await _lock.WaitAsync(linked.Token);
        try
        {
            await EnsureConnectedAsync(linked.Token);
            await WriteFrameAsync(Encoding.UTF8.GetBytes(method + "\n" + body), linked.Token);
            var response = await ReadFrameAsync(linked.Token);
            // The plugin serves one connection at a time: release it after each
            // call so other MCP clients (other Claude sessions) can get in.
            Disconnect();
            using var doc = JsonDocument.Parse(response);
            return doc.RootElement.Clone();
        }
        catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested && !ct.IsCancellationRequested)
        {
            Disconnect();
            throw new McpException(
                $"AutoCAD did not answer '{method}' within {timeout.TotalSeconds:0} s. " +
                "It may be busy with a command or a dialog.");
        }
        catch (Exception ex) when (ex is IOException or JsonException)
        {
            // Only McpException messages reach the client; keep the reason visible.
            Disconnect();
            throw new McpException(ex.Message, ex);
        }
        catch
        {
            Disconnect();
            throw;
        }
        finally
        {
            _lock.Release();
        }
    }

    private async Task EnsureConnectedAsync(CancellationToken ct)
    {
        if (_pipe?.IsConnected == true) return;
        Disconnect();

        int pid = await ResolvePidAsync(ct);
        var pipe = new NamedPipeClientStream(".", PipePrefix + pid, PipeDirection.InOut, PipeOptions.Asynchronous);
        try
        {
            using var connectCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            connectCts.CancelAfter(ConnectTimeoutMs);
            await pipe.ConnectAsync(connectCts.Token);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            pipe.Dispose();
            throw new IOException(
                $"Could not connect to AutoCAD (pid {pid}) within {ConnectTimeoutMs / 1000} s: " +
                "another client is busy with a long call, or a client from an older build keeps the pipe open.");
        }
        _pipe = pipe;
    }

    private async Task<int> ResolvePidAsync(CancellationToken ct)
    {
        // Between two connections the plugin closes and recreates its pipe, so
        // it can be missing for a moment.
        var pids = ListInstances();
        for (int i = 0; i < 5 && (pids.Count == 0 || (_selectedPid is int s && !pids.Contains(s))); i++)
        {
            await Task.Delay(100, ct);
            pids = ListInstances();
        }
        if (_selectedPid is int selected)
        {
            if (pids.Contains(selected)) return selected;
            throw new IOException(
                $"AutoCAD pid {selected} no longer has the bridge running. Use list_instances / select_instance.");
        }
        return pids.Count switch
        {
            0 => throw new IOException(
                "No AutoCAD with ArqaTools is listening. Load the ArqaTools (MCP fork) plugin and run ATMCPSTART."),
            1 => pids[0],
            _ => throw new IOException(
                $"Several AutoCAD instances are listening (pids {string.Join(", ", pids)}). Call select_instance first."),
        };
    }

    private async Task WriteFrameAsync(byte[] payload, CancellationToken ct)
    {
        var frame = new byte[4 + payload.Length];
        BitConverter.TryWriteBytes(frame.AsSpan(0, 4), payload.Length);   // little-endian on Windows
        payload.CopyTo(frame, 4);
        await _pipe!.WriteAsync(frame, ct);
        await _pipe.FlushAsync(ct);
    }

    private async Task<string> ReadFrameAsync(CancellationToken ct)
    {
        var header = new byte[4];
        await _pipe!.ReadExactlyAsync(header, ct);
        int length = BitConverter.ToInt32(header, 0);
        if (length < 0 || length > MaxFrame)
            throw new IOException($"Invalid frame length {length} from AutoCAD.");
        var payload = new byte[length];
        await _pipe.ReadExactlyAsync(payload, ct);
        return Encoding.UTF8.GetString(payload);
    }

    private void Disconnect()
    {
        try { _pipe?.Dispose(); } catch { }
        _pipe = null;
    }

    public void Dispose()
    {
        Disconnect();
        _lock.Dispose();
    }
}
