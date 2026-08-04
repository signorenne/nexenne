# nexenne agent guide

This is the canonical agent-facing code hub for nexenne. It holds the rules an
AI agent or human contributor must keep in working memory before changing the
repo. Deeper explanations live in the linked docs; this file gives the floor,
not the full reference. The documentation map and role matrix live in
[doc/README.org](doc/README.org).

`AGENTS.md` is the single source of truth for coding-assistant guidance in this
repo. If a tool needs another filename, create a tiny bridge that imports or
points to this file; do not copy these rules into a second maintained file.
AI-assisted output is still the human committer's responsibility: review, test,
and understand every line; use assistants, not autopilot, and do not add AI
trailers.

## First load

For any task, start with this file and then load only the docs for your role:

- Scout: map the area and owner before planning.
- Planner: decide scope, files, commands, and risk.
- Implementer: edit code, tests, examples, and docs.
- Reviewer: look for bugs, regressions, missing tests, and rule drift.
- Module author: add or reshape a module.
- Onboarding: understand how the project is used and built.

Use [doc/README.org](doc/README.org) to choose the rest. For multi-step work,
use [doc/agent-workflows.org](doc/agent-workflows.org). A task touching a module
must also read that module's index under `doc/module/<name>/README.org`. A task
touching a public component must read the matching leaf guide under
`doc/module/<name>/<topic>.org` when it exists.

## Working method

Establish context before changing code: the goal, the constraints, the target
hardware, the workflows, and how the existing code already solves nearby
problems. Then reduce uncertainty before acting, not after.

- Scout for reuse before writing new code. Check whether an existing component
  fits or can be composed, and prefer small, modular pieces that combine into
  larger ones over duplicating logic. See [DESIGN.org](DESIGN.org).
- State the intended change and its expected outcome before editing, so each
  step has a predictable result. If the outcome is not clear yet, scout or plan
  more instead of guessing.
- Break work into small, verifiable steps. Each step has one clear purpose, a
  measurable effect, and contained risk.
- Land the work as several small, incremental commits, one logical change each,
  not one large batch. The commit convention is in
  [CONTRIBUTING.org](CONTRIBUTING.org).
- Verify each step before moving on, then widen validation before finishing.
  See "Validation commands" below.

## Repo shape

nexenne is a C++23 monorepo of small, focused modules. Each module is exposed as
the CMake target `nexenne::<name>` and namespace `nexenne::<name>`. Modules are
header-only by default, built by CMake, tested with doctest, and documented with
Org-mode guides plus Doxygen on the public API.

```text
nexenne/
|-- README.org              project overview and quick start
|-- VISION.org              project direction and non-goals
|-- DESIGN.org              API and library surface principles
|-- AGENTS.md               agent working-memory floor
|-- CLAUDE.md               Claude Code bridge that imports AGENTS.md
|-- GEMINI.md               Gemini bridge that imports AGENTS.md
|-- STYLE_GUIDE.org         C++ style and public API documentation rules
|-- CONTRIBUTING.org        workflow, gates, commits, CI
|-- AI_DISCLAIMER.org       AI-assistance accountability policy
|-- doc/                    documentation map, setup docs, module guides
|-- modules/<name>/         module source, tests, Doxygen config
|-- examples/<name>/        runnable examples per module
|-- cmake/                  CMake helpers used by the repo and modules
`-- template/               new-module scaffold and module author guide
```

## Where new work goes

The full layout owner is [doc/file-system.org](doc/file-system.org). Keep this
summary in memory while editing:

- New public API: `modules/<mod>/include/nexenne/<mod>/<topic>.hpp`.
- Module umbrella include: `modules/<mod>/include/nexenne/<mod>/<mod>.hpp`.
- Module tests: `modules/<mod>/tests/test_<topic>.cpp`.
- Runnable example: `examples/<mod>/<topic>.cpp`.
- User guide: `doc/module/<mod>/<topic>.org` and a link from the module index.
- Generated API reference: Doxygen comments in public headers.
- New module: run `./template/new-module.sh <name>` and follow
  [template/MODULE_GUIDE.org](template/MODULE_GUIDE.org).
- Inter-module dependencies: `modules/<mod>/module.deps` only, one dependency
  module name per line.

Do not create a second registry for modules or dependencies. The root build
discovers modules, and `module.deps` is the single source for nexenne module
dependencies.

## Execution recipes

### Change an existing public component

Full workflow: [doc/agent-workflows.org](doc/agent-workflows.org) ->
Public component workflow.

1. Read this file, [doc/README.org](doc/README.org), the module index, and the
   component guide if one exists.
2. Inspect the existing header, test, example, and similar components in the
   same module.
3. Edit the public header and its Doxygen together.
4. Add or update doctest coverage in `modules/<mod>/tests/`.
5. Add or update a runnable example in `examples/<mod>/` when behavior or
   public usage changes.
6. Update the Org guide and module README link so the docs stay navigable.
7. Run the narrow test first, then the repo-level target when the change is not
   trivial.

### Add a new component to an existing module

Follow [CONTRIBUTING.org](CONTRIBUTING.org) -> Adding a component to a module.
The public surface lands in four places: header, tests, example, and guide.
Wire the header and source file names into the module CMake files, examples
CMake file, umbrella header, and module docs index.

### Add a new module

```sh
./template/new-module.sh <name>
```

Then follow [template/MODULE_GUIDE.org](template/MODULE_GUIDE.org). Keep the
module name lowercase snake_case. Declare nexenne dependencies in
`modules/<name>/module.deps`, not in a central file.

### Docs-only change

Docs are Org mode. Do not add Markdown docs. The only Markdown files allowed at
the repo root are agent compatibility entrypoints such as `AGENTS.md`,
`CLAUDE.md`, and `GEMINI.md`; keep `AGENTS.md` canonical and make tool-specific
files point to it instead of duplicating rules. Put setup docs under
`doc/setup/`, module overviews under `doc/module/<name>/README.org`, and
component guides under `doc/module/<name>/<topic>.org`. Register new docs in the
owning index and, for new top-level or setup docs, in [doc/README.org](doc/README.org).

## Hard rules

- C++23, `CXX_EXTENSIONS OFF`, with modern standard-library facilities before
  custom code.
- snake_case for public C++ names, PascalCase only for template parameters.
- Trailing return types, braced initialization, east const, and const by
  default.
- Every public header starts with `#pragma once` and a Doxygen `@file` block.
- Every public symbol has Javadoc-flavored Doxygen: `/** ... */`, no HTML and
  no backticks (use `\c` for inline code). Keep the tag order from
  [STYLE_GUIDE.org](STYLE_GUIDE.org), including `@pre` and `@post`, and
  document every parameter and return.
- Recoverable errors use `std::expected<T, error_t>`. Do not throw for normal
  error reporting.
- Dependency changes follow [doc/dependencies.org](doc/dependencies.org).
- Public API changes need tests, examples, docs, and Doxygen in the same
  change.
- Prose docs are `.org` files only, except the root agent compatibility
  entrypoints named above.
- No em dash or en dash characters in docs, comments, commits, or release text.
  See [doc/writing.org](doc/writing.org).
- No per-file license or authorship banners. The license and authorship live in
  `LICENSE` and git.
- Commit subjects are `type(scope): description` with a required scope, kept to
  a single concise sentence with no body. Split a change that needs more than
  one sentence into smaller commits.

## Validation commands

Use the narrowest useful command while iterating, then widen before finishing.

```sh
cmake --preset dev
cmake --build --preset dev && ctest --preset dev
cmake --preset ci
cmake --build --preset ci && ctest --preset ci
cmake --preset asan
cmake --build --preset asan && ctest --preset asan
cmake -B build/docs -DNEXENNE_BUILD_DOCS=ON
cmake --build build/docs --target nexenne_docs
bash scripts/check_sync.sh
bash scripts/install_smoke.sh
```

`ctest` runs the suites in parallel; `cmake --build --preset <p> --target
nexenne_tests` is the serial build-and-run equivalent.

For one module, run its generated test binary directly, for example:

```sh
./build/dev/bin/nexenne_container_tests --test-case="*ring_buffer*"
```

## Review checklist

- Does the change belong to the module or setup area it touched?
- Is every new public type or function documented and tested?
- Does the example demonstrate the public usage a reader needs?
- Is the dependency recorded in exactly one place?
- Did any doc duplicate a concept that another doc already owns?
- Do the commands in "Validation commands" still match the touched area?
- Was the work landed as small, verifiable steps, each with a clear purpose?
