using System.Globalization;
using System.Text;
using System.Text.Json;

namespace ArqaToolsMcp;

/// <summary>
/// JSON -> Lua table constructor, so the plugin needs no JSON parser
/// (LuaRunOptions::answers). Arrays use explicit indices plus n, so a null
/// (= Enter) does not shorten the list.
/// </summary>
public static class LuaLiteral
{
    public static string FromAnswers(IReadOnlyList<JsonElement> answers)
    {
        var sb = new StringBuilder("{n=").Append(answers.Count);
        for (int i = 0; i < answers.Count; i++)
        {
            if (answers[i].ValueKind == JsonValueKind.Null) continue;
            sb.Append(",[").Append(i + 1).Append("]=");
            Append(sb, answers[i]);
        }
        return sb.Append('}').ToString();
    }

    /// <summary>Named tool arguments -> {["rows"]=3,["base"]={[1]=0,...}}.</summary>
    public static string FromObject(IEnumerable<KeyValuePair<string, JsonElement>> args)
    {
        var sb = new StringBuilder("{");
        foreach (var (key, value) in args)
        {
            if (value.ValueKind == JsonValueKind.Null) continue;
            sb.Append('[');
            AppendString(sb, key);
            sb.Append("]=");
            Append(sb, value);
            sb.Append(',');
        }
        return sb.Append('}').ToString();
    }

    private static void Append(StringBuilder sb, JsonElement e)
    {
        switch (e.ValueKind)
        {
            case JsonValueKind.Number:
                sb.Append(e.TryGetInt64(out long l)
                    ? l.ToString(CultureInfo.InvariantCulture)
                    : e.GetDouble().ToString("R", CultureInfo.InvariantCulture));
                break;
            case JsonValueKind.String: AppendString(sb, e.GetString()!); break;
            case JsonValueKind.True:   sb.Append("true"); break;
            case JsonValueKind.False:  sb.Append("false"); break;
            case JsonValueKind.Null:   sb.Append("nil"); break;
            case JsonValueKind.Array:
            {
                sb.Append('{');
                int i = 0;
                foreach (var item in e.EnumerateArray())
                {
                    i++;
                    if (item.ValueKind == JsonValueKind.Null) continue;
                    sb.Append('[').Append(i).Append("]=");
                    Append(sb, item);
                    sb.Append(',');
                }
                sb.Append('}');
                break;
            }
            case JsonValueKind.Object:
            {
                sb.Append('{');
                foreach (var p in e.EnumerateObject())
                {
                    sb.Append('[');
                    AppendString(sb, p.Name);
                    sb.Append("]=");
                    Append(sb, p.Value);
                    sb.Append(',');
                }
                sb.Append('}');
                break;
            }
            default:
                throw new ArgumentException($"Unsupported JSON value: {e.ValueKind}");
        }
    }

    // Escapes everything outside printable ASCII as \ddd bytes (UTF-8), which Lua decodes exactly.
    private static void AppendString(StringBuilder sb, string s)
    {
        sb.Append('"');
        foreach (byte b in Encoding.UTF8.GetBytes(s))
        {
            if (b == '"' || b == '\\') sb.Append('\\').Append((char)b);
            else if (b >= 0x20 && b < 0x7F) sb.Append((char)b);
            else sb.Append('\\').Append(b.ToString("000", CultureInfo.InvariantCulture));
        }
        sb.Append('"');
    }
}
