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

## Build

```powershell
dotnet publish FileCompressor.Wpf.csproj -c Release -o dist
```

## Run

```powershell
& ".\dist\FileCompressor.exe"
```

## Take It To Another PC

For a school computer, USB drive, or any Windows PC where you do not want to install extra stuff, build the portable ZIP:

```powershell
.\scripts\Build-Portable.ps1
```

That creates:

```text
FileCompressor-portable-win-x64.zip
```

Extract the ZIP on the other computer and run `FileCompressor.exe`.

The portable build includes the .NET runtime. The script also tries to copy `ffmpeg.exe` and `ffprobe.exe` into the folder, so video/image/audio compression can work even when FFmpeg is not installed on that computer. If the script says either one was not found, copy those two FFmpeg files into the portable folder before taking it with you.

## How To Use

1. Open the app.
2. Add files, add a folder, or drag files into the drop area.
3. Choose an output folder.
4. Pick a quality preset.
5. Pick a video encoder if you are compressing videos.
6. Press `Start All`.

When a video finishes, click `Preview` to compare the original and compressed versions side by side.
