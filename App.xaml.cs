using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace FileCompressorModern;

public partial class App : System.Windows.Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        DispatcherUnhandledException += HandleDispatcherException;
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            if (args.ExceptionObject is Exception ex)
            {
                LogStartupError(ex);
            }
        };

        base.OnStartup(e);

        try
        {
            var window = new MainWindow();
            MainWindow = window;
            window.Show();
        }
        catch (Exception ex)
        {
            LogStartupError(ex);
            System.Windows.MessageBox.Show(
                "File Compressor could not start. I wrote the details to startup-error.txt in the app folder.",
                "File Compressor",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    private static void HandleDispatcherException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        LogStartupError(e.Exception);
        System.Windows.MessageBox.Show(
            e.Exception.Message,
            "File Compressor error",
            MessageBoxButton.OK,
            MessageBoxImage.Error);
        e.Handled = true;
    }

    private static void LogStartupError(Exception ex)
    {
        var path = Path.Combine(AppContext.BaseDirectory, "startup-error.txt");
        File.WriteAllText(path, ex.ToString());
    }
}
