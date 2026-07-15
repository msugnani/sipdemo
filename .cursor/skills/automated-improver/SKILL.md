---
name: automated-improver
description: >-
  Use this skill when a task requires multiple iterative rounds of fixing,
  running tests, and correcting compiler/runtime errors for sipdemo (C++/CMake).
---

# Title: Automated Improver Loop

# Description

Triggers an autonomous self-correction cycle that checks terminal output and iterates code changes until the target task passes code health metrics.

# Instructions

When the user triggers this skill, execute the following autonomous loop:

1. **State Initialization:**
   - Read or initialize `tasks/todo.md` to establish the exact target milestone.
   - Skim `CLAUDE.md` and `tasks/lessons.md` for project constraints and past failures.

2. **Execute and Refactor:**
   - Modify the required files using Cursor's codebase editing tools.
   - Stay within C++17 / existing sipdemo architecture unless the todo explicitly expands scope.

3. **Deterministic Verification:**
   - Run from the repo root:
     - `powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1`
     - If the change is build-only or scripts broke the build tree:
       `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Test`
   - Do not hallucinate test passes — only accept a `0` exit code from the shell output.
   - If tests cannot run (missing MSVC/CMake), say so and stop rather than inventing results.

4. **Evaluate & Recurse:**
   - **Success:** Log the victory to `tasks/lessons.md`, clear or rewrite `tasks/todo.md` to reflect done work, and summarize for the user.
   - **Failure:** Analyze the raw terminal stack trace / compiler errors, write why it failed in `tasks/lessons.md`, update `tasks/todo.md` with a new approach, and jump back to Step 2.

# Guardrails

- If the loop fails to make progress or runs for **10 continuous turns**, pause, preserve the state in `tasks/todo.md` / `tasks/lessons.md`, and prompt the developer for manual instruction.
- Do not commit or push unless the user explicitly asks.
- Do not delete `docs/` postmortems; append lessons instead.
