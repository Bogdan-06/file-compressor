using System.Diagnostics;
using System.IO;
using System.Windows;

namespace FileCompressorModern;

public partial class PreviewWindow : Window
{
    private readonly string _originalPath;
    private readonly string _compressedPath;

    public PreviewWindow(string originalPath, string compressedPath)
    {
        InitializeComponent();

        _originalPath = originalPath;
        _compressedPath = compressedPath;

        OriginalPathText.Text = originalPath;
        CompressedPathText.Text = compressedPath;
        OriginalPlayer.Source = new Uri(originalPath);
        CompressedPlayer.Source = new Uri(compressedPath);
    }

    protected override void OnClosed(EventArgs e)
    {
        OriginalPlayer.Stop();
        CompressedPlayer.Stop();
        OriginalPlayer.Source = null;
        CompressedPlayer.Source = null;
        base.OnClosed(e);
    }

    private void PlayBoth_Click(object sender, RoutedEventArgs e)
    {
        OriginalPlayer.Play();
        CompressedPlayer.Play();
    }

    private void PauseBoth_Click(object sender, RoutedEventArgs e)
    {
        OriginalPlayer.Pause();
        CompressedPlayer.Pause();
    }

    private void RestartBoth_Click(object sender, RoutedEventArgs e)
    {
        OriginalPlayer.Position = TimeSpan.Zero;
        CompressedPlayer.Position = TimeSpan.Zero;
        OriginalPlayer.Play();
        CompressedPlayer.Play();
    }

    private void OpenOriginal_Click(object sender, RoutedEventArgs e) => OpenFile(_originalPath);

    private void OpenCompressed_Click(object sender, RoutedEventArgs e) => OpenFile(_compressedPath);

    private void MediaElement_MediaFailed(object sender, ExceptionRoutedEventArgs e)
    {
        MessageText.Text = "Inline preview failed. Try opening the files externally from the buttons on the right.";
    }

    private static void OpenFile(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }

        Process.Start(new ProcessStartInfo
        {
            FileName = path,
            UseShellExecute = true
        });
    }
}
