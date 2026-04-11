<!--
SPDX-FileCopyrightText: 2024 shadLauncher4 Project
SPDX-License-Identifier: GPL-2.0-or-later
-->

<h1 align="center">
  <br>
  <a href="https://shadlaunchers.com/"><img src="https://github.com/shadps4-emu/shadPS4/blob/main/.github/shadps4.png" width="220"></a>
  <br>
  <b>shadLauncher4</b>
  <br>
</h1>

<h1 align="center">
 <a href="https://discord.gg/TxBff4MXaD">
        <img src="https://img.shields.io/discord/1444336480465059880?color=5865F2&label=shadLauncher4%20Discord&logo=Discord&logoColor=white" width="275">
 <a href="https://shadlaunchers.com">
        <img src="https://img.shields.io/badge/shadLaunchers-website-8A2BE2" width="150">
      <a title="Crowdin" target="_blank" href="https://crowdin.com/project/shadlauncher4"><img src="https://badges.crowdin.net/shadlauncher4/localized.svg"></a>
 <a href="https://github.com/shadLaunchers/shadLauncher4/stargazers">
        <img src="https://img.shields.io/github/stars/shadLaunchers/shadLauncher4" width="120">

</h1>

Translations can be done in crowdin page : https://crowdin.com/project/shadlauncher4

Clone the repository with submodules:

```sh
git clone --recursive https://github.com/shadLaunchers/shadLauncher4.git
cd shadLauncher4
```

---
## Building
### Windows

**Additional prerequisites:**

- [Visual Studio 2026](https://visualstudio.microsoft.com/)
- [LLVM/Clang](https://releases.llvm.org/)

**Steps:**

1. Configure:
   ```bat
   cmake --fresh -G Ninja ^
     -B build ^
     -DCMAKE_BUILD_TYPE=Release ^
     -DCMAKE_C_COMPILER=clang-cl ^
     -DCMAKE_CXX_COMPILER=clang-cl
   ```
   If Qt is not found automatically, add `-DCMAKE_PREFIX_PATH="C:/Qt/6.10.0/msvc2022_64"` (adjust to your install path).
2. Build:
   ```bat
   cmake --build build --config Release --parallel
   ```
---

### Linux
## Additional prerequisites
**Arch Linux**
```sh
sudo pacman -S --needed \
  clang cmake ninja \
  qt6-base qt6-tools qt6-multimedia \
  openssl \
  vulkan-headers vulkan-icd-loader \
  alsa-lib libpulse \
  mesa
```

**Debian/Ubuntu**
```sh
sudo apt-get install -y \
  clang cmake ninja-build \
  libssl-dev \
  libvulkan-dev \
  libasound2-dev libpulse-dev \
  libgl1-mesa-dev \
  libxcb-cursor-dev
```
**Steps:**
1. Configure:
   ```sh
   cmake --fresh -G Ninja \
     -B build \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_C_COMPILER=clang \
     -DCMAKE_CXX_COMPILER=clang++
   ```
2. Build:
   ```sh
   cmake --build build --parallel $(nproc)
   ```