# File Compressor

A Windows desktop app for high-quality file and media compression.

It uses:

- FFmpeg for video, image, and audio compression.
- Windows `tar.exe` for ZIP archives of ordinary files or folders.

This is the practical way to make a good compressor: video compression is a serious codec problem, so the app lets FFmpeg do the hard work and gives you simple presets.

## Quality Presets

- Near-transparent: best visual quality, usually still smaller, but not always tiny.
- Balanced: good default for sharing.
- Small: stronger compression, more visible quality loss.

## Video Encoder

- Fast GPU - Auto: uses a hardware video encoder when available, preferring NVIDIA NVENC when it works.
- Best quality - CPU x265: slower, but usually gives the best quality for the file size.
- NVIDIA NVENC / Intel Quick Sync / AMD AMF: manual hardware choices for video compression.

GPU encoding is usually much faster. CPU x265 is often better when the goal is the smallest file that still looks clean.

For video, the app outputs H.265/HEVC MP4 files. The near-transparent preset uses a low CRF value so the result should look extremely close to the original in normal viewing. If the original file is already heavily compressed, the new file might not shrink much and can sometimes become larger.

For normal files and folders, ZIP compression is lossless. It will preserve the original exactly, but already-compressed files such as MP4, JPG, PNG, ZIP, and game assets may not shrink much.

## Requirements

- Windows
- CMake
- A C++17 compiler, such as MSYS2 MinGW
- FFmpeg available on PATH

On this machine, `ffmpeg`, `cmake`, and `g++` are already installed.

## Run The Modern UI

Use this version:

```text
dist\FileCompressor.exe
```

The older C++ Win32 prototype still exists in `build\FileCompressor.exe`, but the WPF app in `dist` is the polished interface.

## Build

```powershell
dotnet publish FileCompressor.Wpf.csproj -c Release -o dist
```

The modern app will be created at:

```text
dist\FileCompressor.exe
```

The original C++ prototype can still be built with:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

The C++ prototype will be created at:

```text
build\FileCompressor.exe
```

## How To Use

1. Open the app.
2. Click `Add Files`, `Add Folder`, or drag files/folders into the drop area.
3. Choose an output folder.
4. Pick `70% - near-transparent` when you care most about quality.
5. Press `Start All`.

The queue shows each item as it runs, the progress bar tracks the batch, and the activity log shows the generated output path and whether each file became smaller.

After a video finishes, use `Preview` to compare the original and compressed videos side by side.
