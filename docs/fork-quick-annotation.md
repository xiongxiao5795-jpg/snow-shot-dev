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
It configures the default tool (shape, arrow, or brush), five independent color presets, width, hold time, and fade duration. Shape variants, corner radii, arrow types and brush appearance are stored independently from screenshot styles and survive restarting the application. The separate quick annotation toolbar editor maintains its own layout and includes a persistent-mark toggle. The default tool is a rounded rectangle. On macOS, Control+4
starts quick annotation; the other quick annotation shortcuts are unset by default. Old fork
shortcuts are migrated only when they still match the earlier defaults; customized bindings stay.
The toolbar opens at the lower right of the primary screen on first activation; a dragged position is retained for subsequent activations. It hides after one gesture unless persistence or the hold shortcut is active. Starting a screenshot hides the toolbar and releases annotation input while retaining the existing marks. With the normal Control+4 action,
mouse input returns immediately after the gesture. The fading keep-mode keeps the toolbar and drawing mode active while marks fade automatically.
Persistent-mark mode keeps the toolbar and drawing mode active and exposes undo and redo for marks across screens. A persistent mark remains until persistence is
disabled or the toolbar is explicitly closed; those marks then fade out. Undo and redo are hidden for temporary marks.

The macOS hold shortcut is the first item in the Quick annotation global-shortcuts group. It defaults to Fn and can be changed to a modifier chord or a custom ordinary key or combination. Older per-tool hold bindings are no longer active. macOS may also reserve Fn for input switching, emoji, or dictation depending on system settings and keyboard hardware; verify on the target Mac. A short
hold starts annotation, and an ordinary additional key cancels activation to reduce conflicts
with normal keyboard shortcuts. This passive global listener requires Accessibility permission.
While the shortcut remains held, the toolbar stays open and additional gestures can be drawn. On
release, the mode exits and marks fade unless persistence is enabled. If persistence is enabled,
the mode remains active after release; disable persistence, press Escape, or reopen the toolbar
and close it to exit. Settings and toolbar layout belong to the quick-annotation module, not screenshot styles. Ordinary keys remain visible to the foreground application because the listener is passive; macOS system shortcut conflicts cannot always be detected.

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
Release caches identify the compiler and dependencies independently of application CMake files,
so ordinary feature edits can reuse previous C++ and Rust work. Each Cargo snapshot is keyed by
its tracked Rust inputs; unchanged inputs reuse the same snapshot, and the workflow retains the
newest and one previous successful v2 Rust snapshot for this branch. It checks the new snapshot
exists before deleting older ones. C++ compiler results are keyed separately and remain subject to
ccache's own correctness checks, with the newest and one prior C++ snapshot retained. The Apple
linker also keeps its ThinLTO incremental cache, without
changing the Release optimization mode. Only successful nonempty ThinLTO snapshots up to 3 GiB on
disk are saved; as with Rust, the newest and one prior snapshot are retained per branch. A matching
older cache can seed the first v2 C++/Rust run. Cache deletion uses this repository's Actions
permission and is non-fatal if unavailable. The first compatible
Release run must populate its cache. Rust caches over 9.5 GiB on disk are skipped; GitHub's total
cache quota and eviction can also affect later reuse. Automated offscreen
tests cannot replace manual checks of real mouse pass-through, display layouts, shortcut conflicts,
and meeting-app screen sharing on macOS.
