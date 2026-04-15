using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using FolderBrowserDialog = System.Windows.Forms.FolderBrowserDialog;
using WinFormsDialogResult = System.Windows.Forms.DialogResult;

namespace FileCompressorModern;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<QueueItem> _queue = [];
    private string? _lastOutputPath;
    private string? _lastPreviewOriginalPath;
    private string? _lastPreviewCompressedPath;
    private bool _busy;

    public MainWindow()
    {
        InitializeComponent();
        QueueList.ItemsSource = _queue;
        _queue.CollectionChanged += (_, _) => UpdateQueueSummary();
        OutputFolderBox.Text = Environment.GetFolderPath(Environment.SpecialFolder.MyVideos);
        QualityCombo.SelectionChanged += (_, _) => UpdateQualityHelp();
        EncoderCombo.SelectionChanged += (_, _) => UpdateEncoderHelp();
        UpdateQueueSummary();
        UpdateQualityHelp();
        UpdateEncoderHelp();
    }

    private void AddFiles_Click(object sender, RoutedEventArgs e)
    {
        var filter = string.Join("|",
            "Supported files",
            "*.mp4;*.mov;*.mkv;*.avi;*.wmv;*.webm;*.m4v;*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.mp3;*.wav;*.flac;*.aac;*.m4a;*.ogg;*.opus;*.zip;*.7z;*.rar;*.txt;*.pdf;*.docx;*.xlsx",
            "All files",
            "*.*");

        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Filter = filter,
            CheckFileExists = true,
            Multiselect = true
        };

        if (dialog.ShowDialog(this) == true)
        {
            AddPaths(dialog.FileNames);
        }
    }

    private void AddFolder_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "Choose a folder to compress",
            UseDescriptionForTitle = true,
            ShowNewFolderButton = false
        };

        if (dialog.ShowDialog() == WinFormsDialogResult.OK)
        {
            AddPaths([dialog.SelectedPath]);
        }
    }

    private void BrowseOutput_Click(object sender, RoutedEventArgs e)
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "Choose where compressed files should be saved",
            UseDescriptionForTitle = true,
            ShowNewFolderButton = true,
            SelectedPath = Directory.Exists(OutputFolderBox.Text)
                ? OutputFolderBox.Text
                : Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory)
        };

        if (dialog.ShowDialog() == WinFormsDialogResult.OK)
        {
            OutputFolderBox.Text = dialog.SelectedPath;
        }
    }

    private void DropZone_DragOver(object sender, System.Windows.DragEventArgs e)
    {
        e.Effects = e.Data.GetDataPresent(System.Windows.DataFormats.FileDrop)
            ? System.Windows.DragDropEffects.Copy
            : System.Windows.DragDropEffects.None;
        e.Handled = true;
    }

    private void DropZone_Drop(object sender, System.Windows.DragEventArgs e)
    {
        if (e.Data.GetData(System.Windows.DataFormats.FileDrop) is string[] paths && paths.Length > 0)
        {
            AddPaths(paths);
        }
    }

    private void RemoveQueueItem_Click(object sender, RoutedEventArgs e)
    {
        if ((sender as FrameworkElement)?.DataContext is QueueItem item && !_busy)
        {
            _queue.Remove(item);
        }
    }

    private void ClearQueue_Click(object sender, RoutedEventArgs e)
    {
        if (!_busy)
        {
            _queue.Clear();
            LogBox.Text = "Queue cleared." + Environment.NewLine;
            StatusText.Text = "Ready";
            SetProgress(0);
            _lastOutputPath = null;
            _lastPreviewOriginalPath = null;
            _lastPreviewCompressedPath = null;
            OpenOutputButton.IsEnabled = false;
            PreviewButton.IsEnabled = false;
        }
    }

    private async void StartAll_Click(object sender, RoutedEventArgs e)
    {
        if (_busy)
        {
            return;
        }

        if (_queue.Count == 0)
        {
            System.Windows.MessageBox.Show(this, "Add one or more files first.", "Empty queue", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        var outputFolder = OutputFolderBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(outputFolder) || !Directory.Exists(outputFolder))
        {
            System.Windows.MessageBox.Show(this, "Choose a valid output folder.", "Missing output folder", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        SetBusy(true);
        LogBox.Clear();
        _lastOutputPath = null;
        _lastPreviewOriginalPath = null;
        _lastPreviewCompressedPath = null;
        OpenOutputButton.IsEnabled = false;
        PreviewButton.IsEnabled = false;
        SetProgress(0);

        var items = _queue.ToList();
        var completed = 0;
        var failed = 0;
        ResetLatestComparison();

        for (var index = 0; index < items.Count; index++)
        {
            var item = items[index];
            item.Status = "Working";
            item.ResultText = "";
            StatusText.Text = $"Compressing {index + 1} of {items.Count}: {item.Name}";

            try
            {
                var job = new CompressionJob(item.Path, outputFolder, QualityCombo.SelectedIndex, EncoderCombo.SelectedIndex);
                var progressStart = (index / (double)items.Count) * 100.0;
                var progressSpan = 100.0 / items.Count;
                SetProgress(progressStart);
                var result = await Task.Run(() => RunCompression(job, progressStart, progressSpan));
                item.Status = result.WasBigger ? "Bigger" : "Done";
                item.ResultText = result.ComparisonText;
                _lastOutputPath = result.OutputPath;
                if (result.IsVideo)
                {
                    _lastPreviewOriginalPath = result.InputPath;
                    _lastPreviewCompressedPath = result.OutputPath;
                }
                ShowLatestComparison(result);
                completed++;
            }
            catch (Exception ex)
            {
                item.Status = "Failed";
                item.ResultText = ex.Message;
                failed++;
                AppendLog(Environment.NewLine + $"Error for {item.Name}: {ex.Message}" + Environment.NewLine);
            }

            SetProgress(((index + 1) / (double)items.Count) * 100.0);
        }

        OpenOutputButton.IsEnabled = !string.IsNullOrWhiteSpace(_lastOutputPath);
        PreviewButton.IsEnabled = CanPreviewLatest();
        StatusText.Text = failed == 0
            ? $"Finished {completed} item{Plural(completed)}."
            : $"Finished {completed}, failed {failed}.";
        SetBusy(false);
    }

    private void Preview_Click(object sender, RoutedEventArgs e)
    {
        if (!CanPreviewLatest())
        {
            System.Windows.MessageBox.Show(this, "Compress a video first, then the preview will be available.", "No preview yet", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        var preview = new PreviewWindow(_lastPreviewOriginalPath!, _lastPreviewCompressedPath!)
        {
            Owner = this
        };
        preview.Show();
    }

    private void OpenOutput_Click(object sender, RoutedEventArgs e)
    {
        var target = _lastOutputPath;
        if (string.IsNullOrWhiteSpace(target))
        {
            return;
        }

        if (File.Exists(target))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = "/select,\"" + target + "\"",
                UseShellExecute = true
            });
            return;
        }

        if (Directory.Exists(OutputFolderBox.Text))
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = OutputFolderBox.Text,
                UseShellExecute = true
            });
        }
    }

    private CompressionResult RunCompression(CompressionJob job, double progressStart, double progressSpan)
    {
        var kind = DetectKind(job.InputPath);
        AppendLog($"Input: {job.InputPath}{Environment.NewLine}");
        AppendLog($"Mode: {KindName(kind)}{Environment.NewLine}");

        if (kind != CompressionKind.Archive)
        {
            AppendLog($"Quality: {QualityName(job.QualityIndex)}{Environment.NewLine}");
        }
        else
        {
            AppendLog("ZIP is lossless. Already-compressed files may not shrink much." + Environment.NewLine);
        }

        var spec = BuildProcessSpec(job, kind);
        AppendLog($"{Environment.NewLine}{spec.Description}{Environment.NewLine}");
        AppendLog($"Tool: {spec.Executable}{Environment.NewLine}");
        AppendLog($"Output: {spec.OutputPath}{Environment.NewLine}{Environment.NewLine}");

        var duration = kind == CompressionKind.Video ? GetMediaDurationSeconds(job.InputPath) : 0;
        var exitCode = RunProcess(spec, duration, progressStart, progressSpan);
        if (exitCode != 0)
        {
            TryDeleteFailedOutput(spec.OutputPath);
            throw new InvalidOperationException($"Compression failed with exit code {exitCode}.");
        }

        var before = PathSize(job.InputPath);
        var after = PathSize(spec.OutputPath);
        AppendLog(Environment.NewLine + "Finished successfully." + Environment.NewLine);

        if (before > 0 && after > 0)
        {
            AppendLog($"Original: {FormatBytes(before)}{Environment.NewLine}");
            AppendLog($"Compressed: {FormatBytes(after)}{Environment.NewLine}");

            if (after < before)
            {
                var saved = (1.0 - (after / (double)before)) * 100.0;
                AppendLog($"Saved: {saved.ToString("0.0", CultureInfo.InvariantCulture)}%{Environment.NewLine}");
            }
            else
            {
                AppendLog("The output is larger. That can happen when the original was already compressed." + Environment.NewLine);
            }
        }

        AppendLog(Environment.NewLine);
        return CompressionResult.Create(job.InputPath, spec.OutputPath, before, after, kind == CompressionKind.Video);
    }

    private int RunProcess(ProcessSpec spec, double durationSeconds, double progressStart, double progressSpan)
    {
        using var process = new Process();
        process.StartInfo = new ProcessStartInfo
        {
            FileName = spec.Executable,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        foreach (var arg in spec.Args)
        {
            process.StartInfo.ArgumentList.Add(arg);
        }

        process.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrWhiteSpace(e.Data))
            {
                if (!HandleProgressLine(e.Data, durationSeconds, progressStart, progressSpan))
                {
                    AppendLog(e.Data + Environment.NewLine);
                }
            }
        };
        process.ErrorDataReceived += (_, e) =>
        {
            if (!string.IsNullOrWhiteSpace(e.Data))
            {
                AppendLog(e.Data + Environment.NewLine);
            }
        };

        try
        {
            process.Start();
        }
        catch (Exception ex) when (ex is Win32Exception or FileNotFoundException)
        {
            var toolName = Path.GetFileName(spec.Executable);
            var helpText = toolName.Equals("tar.exe", StringComparison.OrdinalIgnoreCase)
                ? "Windows tar.exe is required for ZIP archives. It is included with current Windows versions."
                : "Put ffmpeg.exe and ffprobe.exe next to FileCompressor.exe, or install FFmpeg and add it to PATH.";

            throw new InvalidOperationException(
                $"Could not start {toolName}. {helpText}",
                ex);
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        process.WaitForExit();
        return process.ExitCode;
    }

    private ProcessSpec BuildProcessSpec(CompressionJob job, CompressionKind kind)
    {
        var stem = CleanStem(job.InputPath);

        if (kind == CompressionKind.Video)
        {
            return BuildVideoProcessSpec(job, stem);
        }

        if (kind == CompressionKind.Image)
        {
            var quality = job.QualityIndex switch { 0 => "90", 1 => "82", _ => "72" };
            var output = MakeUniqueOutputPath(job.OutputFolder, stem, ".webp");
            return new ProcessSpec(
                FfmpegExecutable(),
                ["-hide_banner", "-nostdin", "-y", "-i", job.InputPath, "-frames:v", "1", "-c:v", "libwebp", "-q:v", quality, output],
                output,
                $"Compressing image as WebP, {QualityName(job.QualityIndex)} preset");
        }

        if (kind == CompressionKind.Audio)
        {
            var bitrate = job.QualityIndex switch { 0 => "192k", 1 => "160k", _ => "96k" };
            var output = MakeUniqueOutputPath(job.OutputFolder, stem, ".m4a");
            return new ProcessSpec(
                FfmpegExecutable(),
                ["-hide_banner", "-nostdin", "-y", "-i", job.InputPath, "-vn", "-c:a", "aac", "-b:a", bitrate, output],
                output,
                $"Compressing audio as AAC, {QualityName(job.QualityIndex)} preset");
        }

        var input = new FileInfo(job.InputPath);
        var parent = Directory.Exists(job.InputPath)
            ? Directory.GetParent(job.InputPath)?.FullName ?? Path.GetPathRoot(job.InputPath) ?? "."
            : input.DirectoryName ?? ".";
        var fileName = Directory.Exists(job.InputPath) ? new DirectoryInfo(job.InputPath).Name : input.Name;
        var archiveOutput = MakeUniqueOutputPath(job.OutputFolder, stem, ".zip");
        return new ProcessSpec(
            ResolveBundledTool("tar.exe"),
            ["-a", "-cf", archiveOutput, "-C", parent, fileName],
            archiveOutput,
            "Creating lossless ZIP archive");
    }

    private ProcessSpec BuildVideoProcessSpec(CompressionJob job, string stem)
    {
        var encoder = ResolveVideoEncoder(job.EncoderIndex);
        var output = MakeUniqueOutputPath(job.OutputFolder, stem, ".mp4");
        var audioBitrate = job.QualityIndex switch { 0 => "192k", 1 => "160k", _ => "128k" };
        var args = new List<string>
        {
            "-hide_banner", "-nostdin", "-y",
            "-progress", "pipe:1",
            "-nostats",
            "-i", job.InputPath,
            "-map", "0:v:0",
            "-map", "0:a?"
        };

        if (encoder == VideoEncoder.CpuX265)
        {
            var crf = job.QualityIndex switch { 0 => "20", 1 => "24", _ => "28" };
            var preset = job.QualityIndex == 0 ? "slow" : "medium";
            args.AddRange([
                "-c:v", "libx265",
                "-preset", preset,
                "-crf", crf,
                "-x265-params", "log-level=error"
            ]);
        }
        else if (encoder == VideoEncoder.NvidiaNvenc)
        {
            var cq = job.QualityIndex switch { 0 => "19", 1 => "23", _ => "28" };
            args.AddRange([
                "-c:v", "hevc_nvenc",
                "-gpu", "0",
                "-preset", "p5",
                "-tune", "hq",
                "-rc", "vbr",
                "-cq", cq,
                "-b:v", "0"
            ]);
        }
        else if (encoder == VideoEncoder.IntelQsv)
        {
            var quality = job.QualityIndex switch { 0 => "21", 1 => "25", _ => "30" };
            args.AddRange([
                "-c:v", "hevc_qsv",
                "-preset", "medium",
                "-global_quality", quality
            ]);
        }
        else
        {
            var qp = job.QualityIndex switch { 0 => "21", 1 => "25", _ => "30" };
            args.AddRange([
                "-c:v", "hevc_amf",
                "-quality", "quality",
                "-rc", "cqp",
                "-qp_i", qp,
                "-qp_p", qp
            ]);
        }

        args.AddRange([
            "-pix_fmt", "yuv420p",
            "-tag:v", "hvc1",
            "-c:a", "aac",
            "-b:a", audioBitrate,
            "-movflags", "+faststart",
            output
        ]);

        return new ProcessSpec(
            FfmpegExecutable(),
            [.. args],
            output,
            $"Compressing video with {VideoEncoderLabel(encoder)}, {QualityName(job.QualityIndex)} preset");
    }

    private void AddPaths(IEnumerable<string> paths)
    {
        var added = 0;

        foreach (var rawPath in paths)
        {
            var path = rawPath.Trim();
            if (string.IsNullOrWhiteSpace(path) || !Path.Exists(path))
            {
                continue;
            }

            if (_queue.Any(item => string.Equals(item.Path, path, StringComparison.OrdinalIgnoreCase)))
            {
                continue;
            }

            _queue.Add(QueueItem.FromPath(path));
            added++;
        }

        if (added > 0)
        {
            var first = _queue.FirstOrDefault();
            var folder = first is null
                ? null
                : Directory.Exists(first.Path)
                    ? Directory.GetParent(first.Path)?.FullName
                    : Path.GetDirectoryName(first.Path);

            if (!string.IsNullOrWhiteSpace(folder))
            {
                OutputFolderBox.Text = folder;
            }

            StatusText.Text = $"Added {added} item{Plural(added)}.";
            AppendLog($"Added {added} item{Plural(added)} to the queue.{Environment.NewLine}");
        }
    }

    private void SetBusy(bool busy)
    {
        _busy = busy;
        StartAllButton.Content = busy ? "Working..." : "Start All";
        StartAllButton.IsEnabled = !busy;
        QualityCombo.IsEnabled = !busy;
        EncoderCombo.IsEnabled = !busy;
        OutputFolderBox.IsEnabled = !busy;
        QueueList.IsEnabled = !busy;
    }

    private void ResetLatestComparison()
    {
        LatestOriginalText.Text = "-";
        LatestCompressedText.Text = "-";
        LatestChangeText.Text = "-";
    }

    private void ShowLatestComparison(CompressionResult result)
    {
        LatestOriginalText.Text = FormatBytes(result.OriginalBytes);
        LatestCompressedText.Text = FormatBytes(result.CompressedBytes);
        LatestChangeText.Text = result.ChangeText;
        PreviewButton.IsEnabled = CanPreviewLatest();
    }

    private bool CanPreviewLatest()
    {
        return !string.IsNullOrWhiteSpace(_lastPreviewOriginalPath)
            && !string.IsNullOrWhiteSpace(_lastPreviewCompressedPath)
            && File.Exists(_lastPreviewOriginalPath)
            && File.Exists(_lastPreviewCompressedPath);
    }

    private void SetProgress(double value)
    {
        value = Math.Clamp(value, 0, 100);
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.Invoke(() => SetProgress(value));
            return;
        }

        ProgressBar.Value = value;
        ProgressPercentText.Text = value.ToString("0", CultureInfo.InvariantCulture) + "%";
    }

    private bool HandleProgressLine(string line, double durationSeconds, double progressStart, double progressSpan)
    {
        var separator = line.IndexOf('=');
        if (separator <= 0)
        {
            return false;
        }

        var key = line[..separator].Trim();
        var value = line[(separator + 1)..].Trim();

        if (TryProgressSeconds(key, value, out var seconds))
        {
            if (durationSeconds > 0)
            {
                var fraction = Math.Clamp(seconds / durationSeconds, 0, 1);
                SetProgress(progressStart + progressSpan * fraction);
            }
            return true;
        }

        return key is "frame"
            or "fps"
            or "stream_0_0_q"
            or "bitrate"
            or "total_size"
            or "out_time"
            or "dup_frames"
            or "drop_frames"
            or "speed"
            or "progress";
    }

    private static bool TryProgressSeconds(string key, string value, out double seconds)
    {
        seconds = 0;

        if (key is "out_time_ms" or "out_time_us")
        {
            if (double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var microseconds))
            {
                seconds = microseconds / 1_000_000.0;
                return true;
            }
        }

        if (key == "out_time" && TimeSpan.TryParse(value, CultureInfo.InvariantCulture, out var time))
        {
            seconds = time.TotalSeconds;
            return true;
        }

        return false;
    }

    private static double GetMediaDurationSeconds(string inputPath)
    {
        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = FfprobeExecutable(),
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            startInfo.ArgumentList.Add("-v");
            startInfo.ArgumentList.Add("error");
            startInfo.ArgumentList.Add("-show_entries");
            startInfo.ArgumentList.Add("format=duration");
            startInfo.ArgumentList.Add("-of");
            startInfo.ArgumentList.Add("default=noprint_wrappers=1:nokey=1");
            startInfo.ArgumentList.Add(inputPath);

            using var process = Process.Start(startInfo);

            if (process is null)
            {
                return 0;
            }

            var output = process.StandardOutput.ReadToEnd().Trim();
            process.WaitForExit(5000);

            return double.TryParse(output, NumberStyles.Float, CultureInfo.InvariantCulture, out var duration)
                ? duration
                : 0;
        }
        catch
        {
            return 0;
        }
    }

    private void UpdateQueueSummary()
    {
        var count = _queue.Count;
        QueueCountText.Text = $"{count} item{Plural(count)}";
        QueuedSummaryText.Text = count.ToString(CultureInfo.InvariantCulture);
        TotalSizeText.Text = FormatBytes(_queue.Sum(item => item.Size));
        EmptyQueueText.Visibility = count == 0 ? Visibility.Visible : Visibility.Collapsed;
        QueueList.Visibility = count == 0 ? Visibility.Collapsed : Visibility.Visible;
    }

    private void UpdateQualityHelp()
    {
        QualityHelpText.Text = QualityCombo.SelectedIndex switch
        {
            0 => "Best when you do not want obvious quality loss.",
            1 => "A practical middle ground for sharing and storage.",
            _ => "Smallest outputs, with more visible quality loss."
        };
    }

    private void UpdateEncoderHelp()
    {
        EncoderHelpText.Text = EncoderCombo.SelectedIndex switch
        {
            0 => "Slowest, but usually best quality-per-megabyte.",
            1 => "Uses the first working hardware encoder automatically, preferring NVIDIA when available.",
            2 => "Very fast on your GTX 1650 Ti. Great for speed, sometimes larger than CPU output.",
            3 => "Uses Intel Quick Sync when the Intel driver exposes it to FFmpeg.",
            _ => "Uses AMD AMF when an AMD GPU and driver are available."
        };
    }

    private void AppendLog(string text)
    {
        Dispatcher.Invoke(() =>
        {
            LogBox.AppendText(text);
            LogBox.ScrollToEnd();
        });
    }

    private static CompressionKind DetectKind(string inputPath)
    {
        if (Directory.Exists(inputPath))
        {
            return CompressionKind.Archive;
        }

        var ext = Path.GetExtension(inputPath).ToLowerInvariant();
        string[] videos = [".mp4", ".mov", ".mkv", ".avi", ".wmv", ".webm", ".m4v", ".flv", ".mpeg", ".mpg"];
        string[] images = [".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tif", ".tiff", ".gif", ".heic", ".avif"];
        string[] audio = [".mp3", ".wav", ".flac", ".aac", ".m4a", ".ogg", ".opus", ".wma", ".aiff", ".alac"];

        if (videos.Contains(ext))
        {
            return CompressionKind.Video;
        }
        if (images.Contains(ext))
        {
            return CompressionKind.Image;
        }
        if (audio.Contains(ext))
        {
            return CompressionKind.Audio;
        }
        return CompressionKind.Archive;
    }

    private static string KindName(CompressionKind kind) => kind switch
    {
        CompressionKind.Video => "video",
        CompressionKind.Image => "image",
        CompressionKind.Audio => "audio",
        _ => "ZIP archive"
    };

    private static string KindShort(CompressionKind kind) => kind switch
    {
        CompressionKind.Video => "VID",
        CompressionKind.Image => "IMG",
        CompressionKind.Audio => "AUD",
        _ => "ZIP"
    };

    private static VideoEncoder ResolveVideoEncoder(int encoderIndex)
    {
        return encoderIndex switch
        {
            1 => PickAutoGpuEncoder(),
            2 => VideoEncoder.NvidiaNvenc,
            3 => VideoEncoder.IntelQsv,
            4 => VideoEncoder.AmdAmf,
            _ => VideoEncoder.CpuX265
        };
    }

    private static VideoEncoder PickAutoGpuEncoder()
    {
        var encoders = GetFfmpegEncoderList();

        if (encoders.Contains("hevc_nvenc", StringComparison.OrdinalIgnoreCase))
        {
            return VideoEncoder.NvidiaNvenc;
        }

        if (encoders.Contains("hevc_qsv", StringComparison.OrdinalIgnoreCase))
        {
            return VideoEncoder.IntelQsv;
        }

        if (encoders.Contains("hevc_amf", StringComparison.OrdinalIgnoreCase))
        {
            return VideoEncoder.AmdAmf;
        }

        return VideoEncoder.CpuX265;
    }

    private static string GetFfmpegEncoderList()
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo
            {
                FileName = FfmpegExecutable(),
                Arguments = "-hide_banner -encoders",
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            });

            if (process is null)
            {
                return "";
            }

            var output = process.StandardOutput.ReadToEnd();
            output += process.StandardError.ReadToEnd();
            process.WaitForExit(3000);
            return output;
        }
        catch
        {
            return "";
        }
    }

    private static string FfmpegExecutable() => ResolveBundledTool("ffmpeg.exe");

    private static string FfprobeExecutable() => ResolveBundledTool("ffprobe.exe");

    private static string ResolveBundledTool(string executableName)
    {
        var localPath = Path.Combine(AppContext.BaseDirectory, executableName);
        if (File.Exists(localPath))
        {
            return localPath;
        }

        var toolsPath = Path.Combine(AppContext.BaseDirectory, "tools", executableName);
        if (File.Exists(toolsPath))
        {
            return toolsPath;
        }

        var ffmpegBinPath = Path.Combine(AppContext.BaseDirectory, "ffmpeg", "bin", executableName);
        if (File.Exists(ffmpegBinPath))
        {
            return ffmpegBinPath;
        }

        return executableName;
    }

    private static string VideoEncoderLabel(VideoEncoder encoder) => encoder switch
    {
        VideoEncoder.NvidiaNvenc => "NVIDIA NVENC HEVC",
        VideoEncoder.IntelQsv => "Intel Quick Sync HEVC",
        VideoEncoder.AmdAmf => "AMD AMF HEVC",
        _ => "CPU x265 HEVC"
    };

    private static string QualityName(int quality) => quality switch
    {
        0 => "near-transparent",
        1 => "balanced",
        _ => "small"
    };

    private static string CleanStem(string inputPath)
    {
        var stem = Directory.Exists(inputPath) ? new DirectoryInfo(inputPath).Name : Path.GetFileNameWithoutExtension(inputPath);
        return string.IsNullOrWhiteSpace(stem) ? "compressed" : stem;
    }

    private static string MakeUniqueOutputPath(string outputFolder, string stem, string extension)
    {
        var candidate = Path.Combine(outputFolder, $"{stem}-compressed{extension}");
        if (!Path.Exists(candidate))
        {
            return candidate;
        }

        for (var index = 2; index < 1000; index++)
        {
            candidate = Path.Combine(outputFolder, $"{stem}-compressed-{index}{extension}");
            if (!Path.Exists(candidate))
            {
                return candidate;
            }
        }

        return Path.Combine(outputFolder, $"{stem}-compressed-{DateTime.Now:yyyyMMdd-HHmmss}{extension}");
    }

    private static void TryDeleteFailedOutput(string outputPath)
    {
        try
        {
            if (File.Exists(outputPath) && new FileInfo(outputPath).Length == 0)
            {
                File.Delete(outputPath);
            }
        }
        catch
        {
            // Best effort cleanup only.
        }
    }

    private static long PathSize(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                return new FileInfo(path).Length;
            }

            if (Directory.Exists(path))
            {
                return Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories)
                    .Select(file =>
                    {
                        try
                        {
                            return new FileInfo(file).Length;
                        }
                        catch
                        {
                            return 0L;
                        }
                    })
                    .Sum();
            }
        }
        catch
        {
            return 0;
        }

        return 0;
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "KB", "MB", "GB", "TB"];
        double value = bytes;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }

        return unit == 0
            ? $"{bytes} {units[unit]}"
            : $"{value.ToString("0.00", CultureInfo.InvariantCulture)} {units[unit]}";
    }

    private static string Plural(int count) => count == 1 ? "" : "s";

    private sealed record CompressionJob(string InputPath, string OutputFolder, int QualityIndex, int EncoderIndex);

    private sealed record ProcessSpec(string Executable, string[] Args, string OutputPath, string Description);

    private sealed record CompressionResult(
        string InputPath,
        string OutputPath,
        long OriginalBytes,
        long CompressedBytes,
        string ComparisonText,
        string ChangeText,
        bool WasBigger,
        bool IsVideo)
    {
        public static CompressionResult Create(string inputPath, string outputPath, long originalBytes, long compressedBytes, bool isVideo)
        {
            if (originalBytes <= 0 || compressedBytes <= 0)
            {
                return new CompressionResult(inputPath, outputPath, originalBytes, compressedBytes, "Comparison unavailable", "-", false, isVideo);
            }

            var difference = compressedBytes - originalBytes;
            var percent = Math.Abs(difference) / (double)originalBytes * 100.0;

            if (difference <= 0)
            {
                var savedBytes = Math.Abs(difference);
                var comparison = $"{FormatBytes(originalBytes)} -> {FormatBytes(compressedBytes)} | saved {percent.ToString("0.0", CultureInfo.InvariantCulture)}%";
                var change = $"-{FormatBytes(savedBytes)} ({percent.ToString("0.0", CultureInfo.InvariantCulture)}% smaller)";
                return new CompressionResult(inputPath, outputPath, originalBytes, compressedBytes, comparison, change, false, isVideo);
            }

            var larger = $"{FormatBytes(originalBytes)} -> {FormatBytes(compressedBytes)} | larger by {percent.ToString("0.0", CultureInfo.InvariantCulture)}%";
            var largerChange = $"+{FormatBytes(difference)} ({percent.ToString("0.0", CultureInfo.InvariantCulture)}% bigger)";
            return new CompressionResult(inputPath, outputPath, originalBytes, compressedBytes, larger, largerChange, true, isVideo);
        }
    }

    private enum CompressionKind
    {
        Video,
        Image,
        Audio,
        Archive
    }

    private enum VideoEncoder
    {
        CpuX265,
        NvidiaNvenc,
        IntelQsv,
        AmdAmf
    }

    private sealed class QueueItem : INotifyPropertyChanged
    {
        private string _status = "Ready";
        private string _resultText = "";

        private QueueItem(string path, string name, string displayPath, CompressionKind kind, long size)
        {
            Path = path;
            Name = name;
            DisplayPath = displayPath;
            Kind = kind;
            KindShort = MainWindow.KindShort(kind);
            Size = size;
            SizeText = FormatBytes(size);
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        public string Path { get; }

        public string Name { get; }

        public string DisplayPath { get; }

        public CompressionKind Kind { get; }

        public string KindShort { get; }

        public long Size { get; }

        public string SizeText { get; }

        public string Status
        {
            get => _status;
            set
            {
                if (_status == value)
                {
                    return;
                }

                _status = value;
                OnPropertyChanged();
            }
        }

        public string ResultText
        {
            get => _resultText;
            set
            {
                if (_resultText == value)
                {
                    return;
                }

                _resultText = value;
                OnPropertyChanged();
            }
        }

        public static QueueItem FromPath(string path)
        {
            var isDirectory = Directory.Exists(path);
            var name = isDirectory ? new DirectoryInfo(path).Name : System.IO.Path.GetFileName(path);
            if (string.IsNullOrWhiteSpace(name))
            {
                name = path;
            }

            var displayPath = isDirectory
                ? Directory.GetParent(path)?.FullName ?? path
                : System.IO.Path.GetDirectoryName(path) ?? path;

            var kind = DetectKind(path);
            return new QueueItem(path, name, displayPath, kind, PathSize(path));
        }

        private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
