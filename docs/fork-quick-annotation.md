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

The Settings > Quick annotation page uses the same settings-row controls as Screenshot settings.
It configures the default tool, color, width, shadow radius (0–25 pt), hold time, fade duration,
and optional toolbar controls. The default tool is a rounded rectangle. On macOS, Control+4
starts quick annotation; the other quick annotation shortcuts are unset by default. Old fork
shortcuts are migrated only when they still match the earlier defaults; customized bindings stay.
The toolbar opens on activation and hides after one gesture. With the normal Control+4 action,
mouse input returns immediately after the gesture. A persistent mark remains until persistence is
disabled or the toolbar is explicitly closed; those marks then fade out.

A separate, configurable macOS modifier-only hold shortcut defaults to Command+Option. A short
hold starts annotation, and an ordinary additional key cancels activation to reduce conflicts
with normal keyboard shortcuts. This passive global listener requires Accessibility permission.
While the shortcut remains held, additional gestures can be drawn after the toolbar hides. On
release, the mode exits and marks fade unless persistence is enabled. If persistence is enabled,
the mode remains active after release; disable persistence, press Escape, or reopen the toolbar
and close it to exit. Settings belong to `quick_annotation/options`, not screenshot styles.

Release artifacts include the upstream installer and the matching DMG checksum. The installer can
stage a locally signed app with a persistent identity; future updates must reuse that identity and
the same installation path. Migration from earlier ad-hoc builds may require a one-time privacy
prompt. macOS controls privacy decisions, so consistent signing cannot guarantee retention for
every macOS release or every update method. Dragging the ad-hoc DMG app directly bypasses the
persistent-identity installer. Do not replace a separate upstream installation by accident.

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
The first compatible Release run must populate its cache. Rust caches over 9.5 GiB on disk are
skipped; GitHub's total cache quota and eviction can also affect later reuse. Automated offscreen
tests cannot replace manual checks of real mouse pass-through, display layouts, shortcut conflicts,
and meeting-app screen sharing on macOS.
