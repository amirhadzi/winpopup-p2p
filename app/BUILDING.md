# Build WinPopup P2P on Windows x64

## Tools

- Windows 10 or 11 x64.
- Visual Studio 2022 (Community or Build Tools), with **Desktop development with C++**, MSVC v143 x64, the Windows SDK, and CMake.
- PowerShell and Windows `tar.exe`.

The application needs no language package manager or web frontend toolchain. Python/Pillow is only used to optionally regenerate the included `resources/app.ico`; it is not required for a build.

## Source package layout

Keep the two folders together:

```text
WinPopupP2P-source/
  app/                  application source and tests
  deps/                 pinned upstream source inputs and dependency build script
```

From the extracted source directory:

```powershell
& .\deps\build-dependencies.ps1
& .\app\build.ps1 -Test
```

These scripts locate Visual Studio, import the compiler environment for this process, and build x64 Release libraries/executables with the static C/C++ runtime. Dependency inputs are verified against pinned hashes. Included archives permit rebuilding without downloading them; the dependency script can retrieve missing archives from their upstream sources.

The result is `app\build\WinPopup.exe`. It needs only Windows system DLLs. To distribute, include the license notices and matching source package.

If you place the compiled dependencies somewhere else:

```powershell
& .\app\build.ps1 -Dependencies 'C:\path\to\deps\install' -Test
```

The scripts use the NMake CMake generator. Use a fresh build directory if switching generators. Do not combine Debug libraries with the Release dependency build.

## Verification tools

`app\build\core_tests.exe <temporary-folder>` creates disposable identities and exercises the actual Tox transport over loopback, without public bootstrap nodes. Tests cover contact requests and approval, direct connectivity, Unicode messaging, delivery receipts, profile encryption, identity/contact persistence, and damaged-profile protection.

`app\build\network_probe.exe <temporary-folder>` is a separate, opt-in public-network connectivity check. It contacts the pinned public Tox nodes and waits up to 90 seconds. It does not contact another person or transmit a chat message. A successful check demonstrates network discovery from the test machine, not connectivity through every router or firewall.

`WinPopup.exe --preview-ui` opens an empty interface preview without accessing a profile or starting networking. `--preview-compose` also opens Send Message. `--smoke-ui` automatically closes the main preview. These switches are for visual/smoke testing, not normal messaging.

The delivered build is unsigned. Signing with a trusted Windows code-signing certificate is a separate distribution step.

`app\build\ui_render.exe <existing-output-folder>` writes 12 BMP layout fixtures using the production GDI drawing functions and native controls: the received-message window, Send Message, their combined arrangement, and profile setup, at 96/120/144 DPI. These contain illustrative test messages and do not open a profile or connect to the network. Offscreen Windows control rendering can omit non-client details such as scrollbars; these are layout fixtures, not desktop screenshots.

`app\build\ui_behavior_tests.exe` creates hidden production windows and exercises received-message navigation/deletion, recipient selection, draft retention, delivery acknowledgement mapping, and delayed-send events. CTest runs these checks alongside the real-Tox core integration test when you use `-Test`.
