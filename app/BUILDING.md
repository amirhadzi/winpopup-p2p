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

The small MIT-licensed Nayuki QR encoder is vendored under `app/third_party/qrcodegen`; its version, origin and hashes are recorded there. It adds no runtime DLL. These scripts locate Visual Studio, import the compiler environment for this process, and build x64 Release libraries/executables with the static C/C++ runtime. Dependency inputs are verified against pinned hashes. The Tox build applies the included, hash-verified `deps/toxcore-idempotent-cancel.patch`, which stops redundant cancellation packets from cancelling a later file transfer. Included archives permit rebuilding without downloading them; the dependency script can retrieve missing archives from their upstream sources.

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

`app\build\ui_render.exe <existing-output-folder>` writes 27 BMP layout fixtures using the production GDI drawing functions and native controls: the received-message window, Send Message, their combined arrangement, profile setup, invitation QR and clipboard bitmap, and offered/progress/saved file transfers, at 96/120/144 DPI. These contain illustrative test messages and synthetic invitations and do not open a profile or connect to the network. Offscreen Windows control rendering can omit non-client details such as scrollbars; these are layout fixtures, not desktop screenshots.

`app\build\ui_behavior_tests.exe` creates hidden production windows and exercises received-message navigation/deletion, recipient selection, draft retention, delivery acknowledgement mapping, delayed-send events, file-transfer controls and history, and QR clipboard format/orientation. CTest runs these checks alongside the real-Tox core/file integration and QR tests when you use `-Test`.

`app\build\file_transfer_tests.exe` exercises two actual Tox peers using disposable temporary profiles and files. It verifies binary/image/empty-file transfer, Unicode paths, explicit acceptance, cancellation, collision protection, failure cleanup, independent/reverse transfers and resource limits.

`app\build\legacy_peer_tests.exe` connects to a raw Tox peer that does not implement the WinPopup file extension. It verifies bidirectional Unicode text and receipts, bounded failure without file data for unsupported outgoing offers, and rejection of unsupported incoming files without creating downloads.

`app\build\qr_tests.exe [existing-output-folder]` validates invitation normalization, QR generation and invalid input. The optional output folder receives synthetic invitation PGM fixtures and their expected text payloads for an independent QR decoder. No real identity or secret is used.
