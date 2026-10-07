// AiHarness.h - the loop around the model for AI-written Lua (ATAICMD, ATAILUA)
//
// The model only produces text. The harness turns that text into something
// safe to accept:
//
//   context  ->  model call  ->  cleanup  ->  validation  ->  test run  ->  review
//      ^                                          |              |            |
//      +------------- correction round <----------+--------------+-- FAIL ----+
//
//   then Present() shows the result and Approve() asks the user (human gate).
//
// A Task supplies what differs per use: the initial context (messages), how
// to validate the code, and how to test-run it. The steps that are the same
// for every use (model call, response cleanup, retries, the AI review with
// the plan-view image, the approval wording) live here. Settings come from
// the `harness` table in ai_config.lua (AiConfig::HarnessSettings).
// See HARNESS.md.

#pragma once
#include "StdAfx.h"
#include "AITools.h"
#include "CommandTester.h"
#include <functional>
#include <vector>

namespace AiHarness
{
    enum class Verdict { Pass, Fail, NotReviewed };

    struct Task
    {
        CString label;     // shown to the user and the reviewer: "ATSTAIR", "ATAILUA script"
        CString request;   // what the user asked for, verbatim (judged by the review)
        // Context for the first model call; the harness appends each correction round.
        std::vector<AITools::ChatMessage> messages;
        // Required: accept the cleaned code, or explain why not (sent back to the model).
        std::function<bool(const CString& code, CString& err)> validate;
        // Optional: run the validated code unattended and report what it did.
        std::function<CommandTester::Result(const CString& code)> testRun;
    };

    struct Outcome
    {
        bool    ok = false;       // `code` passed validation
        CString code;             // the accepted code, or the last attempt when !ok
        CString response;         // the model reply that produced `code`
        CString error;            // why there is no valid code, or a later round's failure
        int     attempts = 0;
        CommandTester::Result test;
        Verdict verdict = Verdict::NotReviewed;
        CString reviewText;
    };

    // Runs the loop for `task` with the settings from ai_config.lua.
    Outcome Run(Task& task);

    // Prints the code, the test run, the plan-view path and the verdict, and
    // offers to open the image.
    void Present(const Outcome& o);

    // Human gate. On FAIL: refused when harness.block_on_fail, else
    // "<action> anyway? <No>". Otherwise "<action>? <Yes>", or no question at
    // all when askWhenOk is false. True = go ahead.
    bool Approve(const Outcome& o, const CString& action, bool askWhenOk);

    // The test run as text (console, review prompt, correction round).
    CString DescribeTestRun(const CommandTester::Result& t);

    // Documents\ArqaTools\LuaCommands\test (plan-view images).
    CString TestFolder();
}
