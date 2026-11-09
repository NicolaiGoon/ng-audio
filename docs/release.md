# CI and releases

Two GitHub Actions workflows build Ng DAW:

- **CI** (`.github/workflows/ci.yml`) builds and tests every pull request and every push to `main`
  on Windows and Linux.
- **Release** (`.github/workflows/release.yml`) builds the Windows installer, checks that it
  installs and runs, and on a version tag publishes a GitHub release with it.

- [CI](#ci)
- [The release workflow](#the-release-workflow)
- [The Windows installer](#the-windows-installer)
- [Cutting a release](#cutting-a-release)
- [Building the installer locally](#building-the-installer-locally)

## CI

### Triggers

```yaml
on:
  push:
    branches: [main]
  pull_request:
```

Runs are grouped per ref (`ci-${{ github.ref }}`) with `cancel-in-progress: true`, so pushing to a
pull request again cancels its previous run.

### Jobs

One `build` job, as a matrix with `fail-fast: false` (one platform failing doesn't cancel the
other):

| Name | Runner | Compiler | Compiler cache |
| --- | --- | --- | --- |
| Windows (MSVC) | `windows-2022` | MSVC (via `ilammy/msvc-dev-cmd@v1`) | sccache |
| Linux (GCC) | `ubuntu-24.04` | GCC | ccache |

macOS is not built in CI.

Both build with **Ninja** and a **Debug** configuration, and keep the Tracktion and JUCE downloads
in `${{ github.workspace }}/.deps` (`FETCHCONTENT_BASE_DIR`) so they can be cached.

### Steps

1. `actions/checkout@v4`.
2. **Install Linux dependencies** (Linux only):
   ```sh
   sudo apt-get update
   sudo apt-get install -y ninja-build ccache libasound2-dev libjack-jackd2-dev libfreetype-dev \
     libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev xvfb
   ```
3. **Set up MSVC for Ninja** (Windows only): `ilammy/msvc-dev-cmd@v1`.
4. **Compiler cache**: `hendrikmuhs/ccache-action@v1.2` with `variant: sccache|ccache`,
   `key: <cache>-<OS>`, `max-size: 2G`.
5. **Cache Tracktion and JUCE sources**: `actions/cache@v4` on `.deps`, key
   `deps-${{ runner.os }}-${{ hashFiles('CMakeLists.txt') }}` (any change to `CMakeLists.txt`
   downloads them again).
6. **Configure**:
   ```sh
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_BASE_DIR="${{ env.FETCHCONTENT_BASE_DIR }}" -DNG_COMPILER_LAUNCHER=${{ matrix.cache }} -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
   ```
   Debug info is embedded (`/Z7`) because sccache can't cache MSVC's shared PDB (`/Zi`); see
   [building.md](building.md#build-caching) for why the cache goes through `NG_COMPILER_LAUNCHER`.
7. **Build**: `cmake --build build --config Debug --parallel` (everything, including the tests and
   test plugins).
8. **Test**:
   - Windows: `ctest --test-dir build -C Debug --output-on-failure`
   - Linux: `xvfb-run -a ctest --test-dir build -C Debug --output-on-failure` (the tests need an X
     display for JUCE's GUI initialisation)

CI produces no artifacts. See [testing.md](testing.md) for what the tests cover.

## The release workflow

### Triggers

```yaml
on:
  push:
    tags: ['v*']
  pull_request:
    paths:
      - 'packaging/**'
      - 'CMakeLists.txt'
      - '.github/workflows/release.yml'
  workflow_dispatch:
```

| Trigger | What happens |
| --- | --- |
| A pushed tag starting with `v` (e.g. `v1.0.0`) | Builds, checks and uploads the installer, **and publishes a GitHub release** |
| A pull request that changes `packaging/`, `CMakeLists.txt` or `release.yml` | Builds, checks and uploads the installer as a workflow artifact; no release |
| Run manually (*Actions > Release > Run workflow*) | Same as a pull request: an artifact, no release |

Runs are grouped per ref (`release-${{ github.ref }}`) with `cancel-in-progress: true`. The
workflow has `permissions: contents: write`, which publishing a release needs.

### The job: `windows-installer` (on `windows-2022`)

1. **Checkout**, and the same `.deps` cache as CI (`deps-Windows-<hash of CMakeLists.txt>`). There's
   no compiler cache here: the release build is a clean Visual Studio build.
2. **Read the version** from `CMakeLists.txt`:
   ```powershell
   $version = (Select-String -Path CMakeLists.txt -Pattern 'project\(NgDaw VERSION ([0-9.]+)').Matches[0].Groups[1].Value
   ```
   On a tag run, the tag minus its leading `v` must equal that version, or the job fails with
   `Tag v<tag> doesn't match the version in CMakeLists.txt (<version>)`. The version is passed on as
   `steps.version.outputs.version`.
3. **Configure** with the Visual Studio generator:
   ```sh
   cmake -B build -G "Visual Studio 17 2022" -DFETCHCONTENT_BASE_DIR="${{ env.FETCHCONTENT_BASE_DIR }}"
   ```
4. **Build** only the shipped programs, in Release:
   ```sh
   cmake --build build --config Release --parallel --target NgDaw NgDawMcp
   ```
   The tests aren't built or run here; CI covers them.
5. **Build the installer** with Inno Setup 6 (installed with `choco install innosetup --no-progress -y`
   if `ISCC.exe` isn't on the runner):
   ```powershell
   & $iscc /DAppVersion=${{ steps.version.outputs.version }} `
           "/DAppExe=$PWD\build\NgDaw_artefacts\Release\Ng DAW.exe" `
           "/DMcpExe=$PWD\build\NgDawMcp_artefacts\Release\NgDawMcp.exe" `
           "/DOutputDir=$PWD\build\installer" `
           packaging\windows\NgDaw.iss
   ```
6. **Check the installer installs and runs**:
   - Installs silently, per user, into a temp folder:
     `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="$env:RUNNER_TEMP\NgDawInstall"`.
   - Fails if `Ng DAW.exe` or `NgDawMcp.exe` isn't there.
   - Pipes an MCP `initialize` request into the installed server and fails unless the reply
     contains `"serverInfo"`:
     ```powershell
     $reply = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | & "$dir\NgDawMcp.exe"
     ```
   - Uninstalls silently with `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`.
7. **Upload the artifact** (`actions/upload-artifact@v4`): `build/installer/*.exe`, named
   `NgDaw-<version>-Windows-Setup`. Pull request and manual runs end here.
8. **Publish the release** (tag runs only, `softprops/action-gh-release@v2`):
   - name: `Ng DAW <version>`
   - files: `build/installer/*.exe`
   - `generate_release_notes: true` (GitHub writes the notes from the merged pull requests since
     the previous release)

### Artifacts

| What | Name | Where |
| --- | --- | --- |
| Installer | `NgDaw-<version>-Windows-Setup.exe` | Workflow artifact `NgDaw-<version>-Windows-Setup` on every run; attached to the GitHub release on tag runs |

Only Windows is packaged. macOS and Linux users build from source (see
[building.md](building.md)).

## The Windows installer

`packaging/windows/NgDaw.iss` is an Inno Setup 6 script.

### Parameters

The workflow passes these with `/D`; the defaults are for building by hand from the repo:

| Define | Default |
| --- | --- |
| `AppVersion` | `0.0.0` |
| `AppExe` | `..\..\build\NgDaw_artefacts\Release\Ng DAW.exe` |
| `McpExe` | `..\..\build\NgDawMcp_artefacts\Release\NgDawMcp.exe` |
| `OutputDir` | `..\..\build\installer` |

(Relative paths are relative to `packaging/windows/`.)

### What it installs

- **Files**: just `Ng DAW.exe` and `NgDawMcp.exe`, into the install folder. Nothing else is needed:
  the C++ runtime is linked statically (`CMAKE_MSVC_RUNTIME_LIBRARY` in `CMakeLists.txt`), so there's
  no Visual C++ redistributable to install.
- **Install folder**: `{autopf}\Ng DAW`, i.e. `C:\Program Files\Ng DAW` for an all-users install.
  `PrivilegesRequiredOverridesAllowed=dialog` lets the user choose a per-user install instead, which
  goes to `%LOCALAPPDATA%\Programs\Ng DAW` and needs no admin rights.
- **Start menu**: a *Ng DAW* group with *Ng DAW* and *Uninstall Ng DAW*.
- **Desktop icon**: optional, the *Create a desktop icon* task (unchecked by default).
- **Architecture**: x64 only (`ArchitecturesAllowed=x64compatible`, installed in 64-bit mode).
- **After installing**: offers to launch Ng DAW (skipped for silent installs).
- **Uninstaller**: `unins000.exe` in the install folder; the app's icon is shown in *Installed apps*.
- **Output**: `NgDaw-<AppVersion>-Windows-Setup.exe`, LZMA2 solid compression, modern wizard style.
- **App id**: `{6E3F1A52-8C1B-4D7E-9B0A-4C2D5E7F9A11}`. Keep it the same in every release: it's how
  a new version's installer recognises (and upgrades) an installed one.

### File association

The *Open .ngdaw projects with Ng DAW* task (checked by default, under *File types:*) writes, under
`HKA\Software\Classes` (HKLM for an all-users install, HKCU for a per-user one):

| Key | Value |
| --- | --- |
| `.ngdaw` | `NgDaw.Project` |
| `NgDaw.Project` | `Ng DAW project` |
| `NgDaw.Project\DefaultIcon` | `{app}\Ng DAW.exe,0` |
| `NgDaw.Project\shell\open\command` | `"{app}\Ng DAW.exe" "%1"` |

`ChangesAssociations=yes` makes Explorer pick the change up straight away. Uninstalling removes the
`.ngdaw` value and the `NgDaw.Project` key. The app opens a `.ngdaw` path it gets on the command
line, which is how a double-click opens a project.

### Using the installed MCP server

With an all-users install the server is `C:\Program Files\Ng DAW\NgDawMcp.exe`:

```sh
claude mcp add ng-daw -- "C:\Program Files\Ng DAW\NgDawMcp.exe"
```

See [mcp.md](mcp.md).

## Cutting a release

1. **Make sure `main` is green.** The release workflow doesn't run the tests; CI on `main` does.
2. **Bump the version** in `CMakeLists.txt`, the single source of truth:
   ```cmake
   project(NgDaw VERSION 1.1.0 LANGUAGES C CXX)
   ```
   This sets the app's version (`NG_VERSION`, shown by the app), the test plugins' version, and the
   installer's version and file name. Use plain `MAJOR.MINOR.PATCH` digits: the workflow reads the
   version with the pattern `[0-9.]+`.
3. **Update the README** if needed (the *What's here (v1.0)* heading, new features).
4. **Merge it through a pull request.** Because it changes `CMakeLists.txt`, the pull request also
   runs the release workflow, so the installer is built and checked before anything is tagged.
   You can download it from the run's artifacts and try it.
5. **Tag the merge commit on `main` with the same version, prefixed with `v`, and push the tag**:
   ```sh
   git checkout main
   git pull
   git tag v1.1.0 && git push origin v1.1.0
   ```
6. **Watch the Release run** in *Actions*. When it finishes, the release
   *Ng DAW 1.1.0* is on the [releases page](https://github.com/nicolaigoon/ng-audio/releases) with
   `NgDaw-1.1.0-Windows-Setup.exe` attached and generated notes. Edit the notes there if you want.

### If something goes wrong

- **"Tag vX doesn't match the version in CMakeLists.txt (Y)"**: the tag was pushed on a commit
  whose `CMakeLists.txt` has another version. Delete the tag and tag the right commit:
  ```sh
  git push origin :refs/tags/v1.1.0
  git tag -d v1.1.0
  # fix or pick the right commit, then
  git tag v1.1.0 && git push origin v1.1.0
  ```
- **The installer check fails**: run the same steps locally (below) and install with the same
  switches, or download the run's artifact.
- **Re-running**: re-run the failed job from the Actions page; the tag doesn't have to be pushed
  again. If a release was already created, the action updates that release's files.

## Building the installer locally

On Windows, with Inno Setup 6 installed (`choco install innosetup`, or from jrsoftware.org):

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel --target NgDaw NgDawMcp
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DAppVersion=1.0.0 packaging\windows\NgDaw.iss
```

With no `/DAppExe`, `/DMcpExe` or `/DOutputDir`, the script uses its defaults, which point at
`build\...\Release\` and write the installer to `build\installer\`. Pass `/DAppVersion` to match
`CMakeLists.txt`, otherwise it's `0.0.0`.

To test it the way CI does (a per-user install into a scratch folder, then uninstall):

```powershell
$dir = "$env:TEMP\NgDawInstall"
Start-Process (Get-ChildItem build\installer\*.exe | Select-Object -First 1).FullName `
    -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER", "/DIR=`"$dir`"" -Wait
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | & "$dir\NgDawMcp.exe"
Start-Process "$dir\unins000.exe" -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES" -Wait
```
