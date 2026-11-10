# Fork-only quick annotation

Quick annotation is maintained in this fork, independently of upstream's product plans.
The feature implementation and its options live in
`snow_shot/src/presentation/quickannotation/`; its public controller and gesture state live in
`snow_shot/include/snow_shot/presentation/quickannotation*.h`. The feature uses Snow Shot's canvas
and the existing global shortcut, settings, application lifecycle, and macOS window APIs. Those
integration points are deliberately kept in the existing host files, so a merge from upstream may
still require resolving conflicts there. It is not a separately loadable plugin.
The main integration points are `applicationcontroller.cpp`, `globalshortcuttypes.h`,
`globalshortcutmanager.cpp`, the settings catalog/backend and configuration schema, the macOS
window policy, the application CMake source list, and the translation catalogs.

The toolbar provides six stroke-color presets, stroke width (1–20), and hold time
(0.5–30 seconds). The default hold is 1.5 seconds, followed by a 0.6-second fade. Options are
stored under `quick_annotation/options` and do not change the regular screenshot drawing styles.
Global shortcuts remain configurable in Snow Shot's shortcut settings. The last selected tool is
stored separately and survives an application restart.

Keep the feature on `codex/quick-annotation-package`. Syncing the fork's default `main` from
upstream does not update this branch automatically: merge the updated `main` into this branch,
resolve any integration conflicts here, and run the targeted GitHub checks. Do not force-push the
feature branch or replace it with upstream's `main`, because that would discard the fork-only work.
Repository and workflow permissions can affect a sync; verify the resulting commit and tests.
After syncing the fork's `main` on GitHub, one way to bring it into the local feature branch is:

```sh
git switch codex/quick-annotation-package
git fetch origin main
git merge FETCH_HEAD
```

Resolve conflicts without dropping the module or its host integration, then push the feature
branch to run its targeted checks on GitHub. This sequence does not require a local build toolchain.

GitHub Actions runs the quick-annotation state, canvas, shortcuts, settings, and native macOS
checks on the feature branch. The release job builds only the Full edition and uses compiler
caches; commits containing `[skip release]` run the checks without starting a Release package.
The `snow-shot-quick-annotation-state-tests` and `snow-shot-quick-annotation-options-tests`
targets can also be built and run independently for focused checks.
The first compatible Release run must populate its cache. Automated offscreen tests cannot replace
manual checks of real mouse pass-through, display layouts, shortcut conflicts, and meeting-app
screen sharing on macOS.
