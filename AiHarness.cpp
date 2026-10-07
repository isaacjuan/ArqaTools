#include "StdAfx.h"
#include "AiHarness.h"
#include "AiConfig.h"
#include "LuaCommands.h"
#include "LuaTools.h"
#include <ShlObj.h>
#include <shellapi.h>

namespace AiHarness
{
namespace {

CString FromUtf8(const std::string& s)
{
    return CString(CA2T(s.c_str(), CP_UTF8));
}

AiConfig::HarnessSettings Settings()
{
    AiConfig::HarnessSettings s;
    CString err;
    AiConfig::Harness(s, err);   // a broken file keeps the defaults; the model call reports it
    return s;
}

bool AskYesNo(const CString& prompt, bool defaultYes)
{
    acedInitGet(0, _T("Yes No"));
    AcString kw;
    int rc = acedGetKword(prompt, kw);
    if (rc == RTNONE) return defaultYes;
    return rc == RTNORM && CString(kw.kwszPtr()).CompareNoCase(_T("Yes")) == 0;
}

// A second model call judges the test run against the user's request, with
// the plan view attached when the provider reads images. A runtime error
// needs no reviewer: it is a FAIL with the error as the reason.
Verdict Review(const Task& task, const CString& code, const CommandTester::Result& t, CString& text)
{
    if (!t.ran || (!t.ok && !t.skipped.IsEmpty()))
    {
        text = t.ran ? t.skipped : _T("Not test-run: ") + t.skipped;
        return Verdict::NotReviewed;
    }
    if (!t.ok)
    {
        text = _T("The test run stopped with an error:\n") + FromUtf8(t.error);
        return Verdict::Fail;
    }

    acutPrintf(_T("Reviewing the result%s...\n"), t.pngPath.IsEmpty() ? _T("") : _T(" (with plan view image)"));
    CString p;
    p += _T("You are testing Lua code for AutoCAD (ArqaTools plugin) against what the user asked for.\n\n");
    p += _T("USER REQUEST for ") + task.label + _T(": ") + task.request + _T("\n\n");
    p += _T("The code was run once in an empty drawing.\n") + DescribeTestRun(t) + _T("\n");
    CString imageNote = _T("The attached image is a plan view of everything it drew, auto-fitted: +X right, +Y up, ")
                        _T("the red cross is the origin (0,0), blue dots mark polyline vertices, the grid step is ")
                        _T("shown bottom-left.\n\n");
    CString tail;
    tail += _T("CODE:\n") + code + _T("\n\n");
    tail += _T("Decide whether the drawn result does what the user asked: the right kind of geometry, counts, ")
            _T("proportions and directions, nothing missing, no stray, zig-zag, duplicated or overlapping geometry. ")
            _T("Inputs marked (auto) were sample answers, not the user's: judge the result for those values. ")
            _T("Ignore layers and colors unless the request mentions them. Reply with PASS or FAIL alone on the ")
            _T("first line, then at most 8 short lines: for FAIL what is wrong and exactly how to fix the code, ")
            _T("for PASS one line on what you checked.");

    CString response;
    bool withImage = !t.pngPath.IsEmpty();
    if (withImage)
    {
        response = AITools::SendWithImage(p + imageNote + tail, t.pngPath);
        if (response.Find(_T("Error:")) == 0)
        {
            acutPrintf(_T("(image review not available: %s - reviewing the text report only)\n"), (LPCTSTR)response);
            withImage = false;
        }
    }
    if (!withImage)
        response = AITools::SendToGitHubCopilot(p + tail);
    if (response.Find(_T("Error:")) == 0)
    {
        text = _T("Review request failed: ") + response;
        return Verdict::NotReviewed;
    }

    response.Replace(_T("\\n"), _T("\n"));   // ExtractContent leaves \n escaped
    response.Trim();
    text = response;
    CString first = response.SpanExcluding(_T("\r\n"));
    first.Trim(_T(" *#:`\t"));
    first.MakeUpper();
    if (first.Find(_T("FAIL")) == 0) return Verdict::Fail;
    if (first.Find(_T("PASS")) == 0) return Verdict::Pass;
    return Verdict::NotReviewed;
}

} // namespace

CString TestFolder()
{
    CString folder = LuaCommands::CommandsFolder() + _T("\\test");
    SHCreateDirectoryEx(NULL, folder, NULL);
    return folder;
}

CString DescribeTestRun(const CommandTester::Result& t)
{
    CString s;
    if (!t.ran) return _T("Not test-run: ") + t.skipped + _T("\n");
    s += _T("Parameters used: ") + FromUtf8(t.params) + _T("\n");
    if (!t.inputs.empty()) s += _T("Inputs answered:\n") + FromUtf8(t.inputs);
    if (!t.skipped.IsEmpty()) s += _T("Note: ") + t.skipped + _T("\n");
    if (!t.output.empty()) s += _T("Printed output:\n") + FromUtf8(t.output);
    if (!t.ok)              s += _T("It stopped with an error:\n") + FromUtf8(t.error) + _T("\n");
    s += FromUtf8(t.report);
    return s;
}

Outcome Run(Task& task)
{
    AiConfig::HarnessSettings s = Settings();
    Outcome o;
    for (int attempt = 1; attempt <= s.maxAttempts; ++attempt)
    {
        if (attempt == 1) acutPrintf(_T("\nAsking AI...\n"));
        else acutPrintf(_T("Asking AI to correct it (attempt %d of %d)...\n"), attempt, s.maxAttempts);
        o.attempts = attempt;

        CString response = AITools::SendToGitHubCopilotWithHistory(task.messages);
        if (response.Find(_T("Error:")) == 0 || response.GetLength() < 3)
        {
            // Keep an earlier valid result, if any; just report why the loop stopped.
            o.error = response.IsEmpty() ? CString(_T("Error: empty AI response.")) : response;
            return o;
        }
        CString code = LuaTools::CleanAiLuaResponse(response);

        CString feedback, err;
        if (!task.validate(code, err))
        {
            acutPrintf(_T("Validation failed: %s\n"), (LPCTSTR)err);
            if (!o.ok)
            {
                o.code  = code;
                o.error = _T("The AI did not produce valid code: ") + err;
            }
            feedback = _T("That code failed validation:\n") + err;
        }
        else
        {
            o.ok       = true;
            o.code     = code;
            o.response = response;
            o.error.Empty();
            o.test       = CommandTester::Result();
            o.verdict    = Verdict::NotReviewed;
            o.reviewText.Empty();

            if (!s.testRun)        o.reviewText = _T("Not test-run (harness.test_run = false).");
            else if (!task.testRun) o.reviewText = _T("No test run for this kind of code.");
            else
            {
                acutPrintf(_T("Valid. Test-running it in a scratch drawing...\n"));
                o.test = task.testRun(code);
                if (s.review) o.verdict = Review(task, code, o.test, o.reviewText);
                else          o.reviewText = _T("Not reviewed (harness.review = false).");
            }
            if (o.verdict != Verdict::Fail) return o;

            acutPrintf(_T("Review: FAIL\n"));
            feedback = _T("A test run shows the code does not do what was asked:\n") + o.reviewText
                     + _T("\n\nTEST RUN:\n") + DescribeTestRun(o.test);
        }
        if (attempt == s.maxAttempts) break;

        AITools::ChatMessage reply, fix;
        reply.role    = _T("assistant");
        reply.content = response;
        fix.role      = _T("user");
        fix.content   = feedback + _T("\nReturn the corrected complete code only.");
        task.messages.push_back(reply);
        task.messages.push_back(fix);
    }
    return o;
}

void Present(const Outcome& o)
{
    acutPrintf(_T("\n========================================\n%s\n========================================\n"),
               (LPCTSTR)o.code);
    if (o.test.ran || !o.test.skipped.IsEmpty())
        acutPrintf(_T("\n=== TEST RUN ===\n%s"), (LPCTSTR)DescribeTestRun(o.test));
    if (!o.test.pngPath.IsEmpty())
        acutPrintf(_T("Plan view: %s\n"), (LPCTSTR)o.test.pngPath);
    acutPrintf(_T("\n=== REVIEW: %s ===\n%s\n"),
               o.verdict == Verdict::Pass ? _T("PASS") : o.verdict == Verdict::Fail ? _T("FAIL") : _T("not reviewed"),
               (LPCTSTR)o.reviewText);
    if (!o.error.IsEmpty())
        acutPrintf(_T("(The last correction round failed: %s)\n"), (LPCTSTR)o.error);

    if (!o.test.pngPath.IsEmpty() && AskYesNo(_T("\nOpen the plan view image? [Yes/No] <No>: "), false))
        ShellExecute(NULL, _T("open"), o.test.pngPath, NULL, NULL, SW_SHOWNORMAL);
}

bool Approve(const Outcome& o, const CString& action, bool askWhenOk)
{
    if (o.verdict == Verdict::Fail)
    {
        if (Settings().blockOnFail)
        {
            acutPrintf(_T("\nRefused: the review failed it and harness.block_on_fail is true in ai_config.lua.\n"));
            return false;
        }
        return AskYesNo(_T("\nThe review says it does not do what was asked. ") + action
                        + _T(" anyway? [Yes/No] <No>: "), false);
    }
    if (!askWhenOk) return true;
    return AskYesNo(_T("\n") + action + _T("? [Yes/No] <Yes>: "), true);
}

} // namespace AiHarness
