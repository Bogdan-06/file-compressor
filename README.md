# File Compressor

A small Windows app for shrinking videos and other files without making the result look obviously crushed.

The goal is simple: add a few files, pick how hard you want to compress them, run the queue, and check whether the new version is actually worth keeping.

For media files, the app uses FFmpeg. For normal files and folders, it creates ZIP archives with Windows `tar.exe`.

## What It Can Do

- Compress videos to H.265 MP4.
- Compress images to WebP.
- Compress audio to M4A.
- Zip regular files and folders.
- Run multiple items in a queue.
- Show the before and after file size.
- Preview the original and compressed video side by side.

## Quality Presets

- `70% - near-transparent`: use this when you mostly care about keeping the video looking the same.
- `50% - balanced`: usually the first preset worth trying.
- `30% - small`: use this when file size matters more than perfect quality.

Sometimes the compressed video can end up bigger. That usually means the original was already squeezed pretty hard, or the preset is trying to preserve more detail than the source really needs. Try `50%`, `30%`, or `Best quality - CPU x265` if that happens.

## Video Encoders

- `Fast GPU - Auto`: tries hardware encoding first.
- `NVIDIA NVENC`: uses an NVIDIA GPU.
- `Intel Quick Sync`: uses Intel integrated graphics.
- `AMD AMF`: uses an AMD GPU.
- `Best quality - CPU x265`: slower, but often gives the best size-to-quality result.

GPU encoding is best when you want the job done quickly. CPU x265 is better when you are patient and want a smaller file that still looks clean.

## Requirements

- Windows
- .NET 8 SDK or later
- FFmpeg available on PATH

Optional, only for the older C++ prototype:

- CMake
- A C++17 compiler, such as MSYS2 MinGW

## Build

```powershell
dotnet publish FileCompressor.Wpf.csproj -c Release -o dist
```

## Run

```powershell
& ".\dist\FileCompressor.exe"
```

The old C++ prototype is still in the repo. You can build it with:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

## How To Use

1. Open the app.
2. Add files, add a folder, or drag files into the drop area.
3. Choose an output folder.
4. Pick a quality preset.
5. Pick a video encoder if you are compressing videos.
6. Press `Start All`.

When a video finishes, click `Preview` to compare the original and compressed versions side by side.
