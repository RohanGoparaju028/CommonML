---
layout: default
title: Install CommonML
---

# Install CommonML (Windows, macOS, Linux)

CommonML distributes native installers generated via CPack in GitHub Releases so you can run `cml` and `cmlc` directly without invoking CMake manually.

## Prebuilt Installers

Download the package for your platform from the [GitHub Releases](https://github.com/RohanGoparaju028/CommonML/releases) page:

| OS | Package Format | What it installs |
| --- | --- | --- |
| **Windows (x64)** | `.msi` (WiX installer) or `.zip` | `cml.exe`, `cmlc.exe`, `commonml.exe`, stdlib, editors, and `share\commonml\windows\register_cml.cmd` |
| **macOS** | `.pkg` (`productbuild`) or `.tar.gz` | `/usr/local/bin/cml`, `cmlc`, `commonml`, stdlib, editors, and `Info.plist` UTI metadata |
| **Linux** | `.deb` / `.rpm` / `.tar.gz` | `/usr/bin/cml`, `cmlc`, `commonml`, stdlib, editors, and XDG MIME (`commonml.xml`) |

### Windows (`.msi`)

1. Run `commonml-<version>-win64.msi`.
2. Add `C:\Program Files\CommonML\bin` to `PATH` if not added automatically, and optionally run `register_cml.cmd` to associate `.cml` and `.cmlbc` files.
3. Verify in PowerShell or CMD:

```powershell
cmlc --version
cml run hello.cml
```

### macOS (`.pkg`)

1. Open `commonml-<version>-Darwin.pkg` and follow the installer prompt (installs to `/usr/local`).
2. Verify in Terminal:

```sh
cmlc --version
cml run hello.cml
```

### Linux (`.deb` / `.rpm`)

Debian / Ubuntu:

```sh
sudo dpkg -i commonml-*-Linux.deb
```

Fedora / RHEL:

```sh
sudo rpm -i commonml-*-Linux.rpm
```

Update MIME database (handled automatically or manually via):

```sh
sudo update-mime-database /usr/share/mime
```
