#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

constexpr int IDC_INPUT_EDIT = 1001;
constexpr int IDC_BROWSE_FILE = 1002;
constexpr int IDC_BROWSE_FOLDER = 1003;
constexpr int IDC_OUTPUT_EDIT = 1004;
constexpr int IDC_BROWSE_OUTPUT = 1005;
constexpr int IDC_KIND_COMBO = 1006;
constexpr int IDC_QUALITY_COMBO = 1007;
constexpr int IDC_START = 1008;
constexpr int IDC_LOG = 1009;
constexpr int IDC_STATUS = 1010;
constexpr int IDC_OPEN_OUTPUT = 1011;
constexpr int IDC_CENTER_ADD = 1012;
constexpr int IDC_ADD_FILE_MENU = 1013;
constexpr int IDC_ADD_FOLDER_MENU = 1014;

constexpr int ID_LABEL_APP_TITLE = 2001;
constexpr int ID_LABEL_NAV_HOME = 2002;
constexpr int ID_LABEL_NAV_CONVERTER = 2003;
constexpr int ID_LABEL_NAV_DOWNLOADER = 2004;
constexpr int ID_LABEL_NAV_VIDEO_COMPRESSOR = 2005;
constexpr int ID_LABEL_NAV_VIDEO_EDITOR = 2006;
constexpr int ID_LABEL_NAV_MERGER = 2007;
constexpr int ID_LABEL_NAV_SCREEN_RECORDER = 2008;
constexpr int ID_LABEL_NAV_DVD_BURNER = 2009;
constexpr int ID_LABEL_NAV_PLAYER = 2010;
constexpr int ID_LABEL_NAV_TOOLBOX = 2011;
constexpr int ID_LABEL_TAB_COMPRESSING = 2012;
constexpr int ID_LABEL_TAB_FINISHED = 2013;
constexpr int ID_LABEL_DROP_TITLE = 2014;
constexpr int ID_LABEL_SELECTED = 2015;
constexpr int ID_LABEL_SIZE = 2016;
constexpr int ID_LABEL_LOCATION = 2017;

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;

struct AppState {
    HWND hwnd = nullptr;
    HWND inputEdit = nullptr;
    HWND outputEdit = nullptr;
    HWND kindCombo = nullptr;
    HWND qualityCombo = nullptr;
    HWND startButton = nullptr;
    HWND openOutputButton = nullptr;
    HWND centerAddButton = nullptr;
    HWND logEdit = nullptr;
    HWND statusText = nullptr;
    HWND browseFileButton = nullptr;
    HWND browseFolderButton = nullptr;
    HWND browseOutputButton = nullptr;
    HFONT font = nullptr;
    HFONT boldFont = nullptr;
    HFONT titleFont = nullptr;
    RECT dropRect = {};
    RECT tabRect = {};
    RECT selectedNavRect = {};
    std::atomic<bool> busy = false;
    std::wstring lastOutputPath;
};

struct Job {
    std::wstring inputPath;
    std::wstring outputFolder;
    int requestedKind = 0;
    int quality = 0;
};

struct ProcessSpec {
    std::wstring executable;
    std::vector<std::wstring> args;
    std::wstring outputPath;
    std::wstring description;
};

enum class Kind {
    Auto = 0,
    Video = 1,
    Image = 2,
    Audio = 3,
    Archive = 4
};

AppState g_app;

std::wstring GetText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(wcslen(text.c_str()));
    return text;
}

void SetText(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

std::wstring Trim(const std::wstring& value) {
    size_t first = 0;
    while (first < value.size() && iswspace(value[first])) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && iswspace(value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

void AppendLog(const std::wstring& text) {
    const int length = GetWindowTextLengthW(g_app.logEdit);
    SendMessageW(g_app.logEdit, EM_SETSEL, length, length);
    SendMessageW(g_app.logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void PostLog(const std::wstring& text) {
    PostMessageW(g_app.hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(text)));
}

std::wstring FormatWindowsError(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = size && buffer ? buffer : L"Unknown error";
    if (buffer) {
        LocalFree(buffer);
    }
    return Trim(message);
}

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return arg;
    }

    std::wstring result = L"\"";
    size_t backslashes = 0;

    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(ch);
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(ch);
        }
    }

    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring BuildCommandLine(const std::wstring& executable, const std::vector<std::wstring>& args) {
    std::wstring command = QuoteArg(executable);
    for (const auto& arg : args) {
        command += L" ";
        command += QuoteArg(arg);
    }
    return command;
}

std::wstring FindTool(const std::wstring& name) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, buffer, nullptr);
    if (size > 0 && size < MAX_PATH) {
        return buffer;
    }
    return L"";
}

std::wstring BytesToWide(const char* data, DWORD length) {
    if (length == 0) {
        return L"";
    }

    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;

    if (chars == 0) {
        codePage = CP_ACP;
        flags = 0;
        chars = MultiByteToWideChar(codePage, flags, data, static_cast<int>(length), nullptr, 0);
    }

    if (chars == 0) {
        return L"";
    }

    std::wstring text(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(codePage, flags, data, static_cast<int>(length), text.data(), chars);

    for (wchar_t& ch : text) {
        if (ch == L'\r') {
            ch = L'\n';
        }
    }

    return text;
}

bool IsDirectoryPath(const std::wstring& path) {
    std::error_code ec;
    return fs::is_directory(fs::path(path), ec);
}

bool PathExists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(fs::path(path), ec);
}

std::wstring ParentFolderFor(const std::wstring& path) {
    fs::path p(path);
    return p.parent_path().wstring();
}

bool HasExt(const std::wstring& ext, const std::vector<std::wstring>& values) {
    return std::find(values.begin(), values.end(), ext) != values.end();
}

Kind DetectKind(const std::wstring& inputPath, int requestedKind) {
    if (requestedKind != 0) {
        return static_cast<Kind>(requestedKind);
    }

    if (IsDirectoryPath(inputPath)) {
        return Kind::Archive;
    }

    const std::wstring ext = Lower(fs::path(inputPath).extension().wstring());
    const std::vector<std::wstring> videos = {
        L".mp4", L".mov", L".mkv", L".avi", L".wmv", L".webm", L".m4v", L".flv", L".mpeg", L".mpg"
    };
    const std::vector<std::wstring> images = {
        L".jpg", L".jpeg", L".png", L".webp", L".bmp", L".tif", L".tiff", L".gif", L".heic", L".avif"
    };
    const std::vector<std::wstring> audio = {
        L".mp3", L".wav", L".flac", L".aac", L".m4a", L".ogg", L".opus", L".wma", L".aiff", L".alac"
    };

    if (HasExt(ext, videos)) {
        return Kind::Video;
    }
    if (HasExt(ext, images)) {
        return Kind::Image;
    }
    if (HasExt(ext, audio)) {
        return Kind::Audio;
    }
    return Kind::Archive;
}

std::wstring KindName(Kind kind) {
    switch (kind) {
    case Kind::Video:
        return L"video";
    case Kind::Image:
        return L"image";
    case Kind::Audio:
        return L"audio";
    case Kind::Archive:
        return L"ZIP archive";
    default:
        return L"auto";
    }
}

std::wstring QualityName(int quality) {
    switch (quality) {
    case 0:
        return L"near-transparent";
    case 1:
        return L"balanced";
    case 2:
        return L"small";
    default:
        return L"near-transparent";
    }
}

std::wstring CleanStem(const std::wstring& inputPath) {
    fs::path p(inputPath);
    std::wstring stem = IsDirectoryPath(inputPath) ? p.filename().wstring() : p.stem().wstring();
    if (stem.empty()) {
        stem = L"compressed";
    }
    return stem;
}

std::wstring MakeUniqueOutputPath(const std::wstring& outputFolder, const std::wstring& stem, const std::wstring& extension) {
    fs::path folder(outputFolder);
    fs::path candidate = folder / (stem + L"-compressed" + extension);

    std::error_code ec;
    if (!fs::exists(candidate, ec)) {
        return candidate.wstring();
    }

    for (int index = 2; index < 1000; ++index) {
        candidate = folder / (stem + L"-compressed-" + std::to_wstring(index) + extension);
        if (!fs::exists(candidate, ec)) {
            return candidate.wstring();
        }
    }

    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t suffix[64] = {};
    swprintf_s(suffix, L"-%04d%02d%02d-%02d%02d%02d",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return (folder / (stem + L"-compressed" + suffix + extension)).wstring();
}

uintmax_t PathSize(const fs::path& path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        return fs::file_size(path, ec);
    }

    if (!fs::is_directory(path, ec)) {
        return 0;
    }

    uintmax_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

std::wstring FormatBytes(uintmax_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::wstringstream stream;
    if (unit == 0) {
        stream << bytes << L" " << units[unit];
    } else {
        stream.setf(std::ios::fixed);
        stream.precision(2);
        stream << value << L" " << units[unit];
    }
    return stream.str();
}

ProcessSpec BuildProcessSpec(const Job& job) {
    const Kind kind = DetectKind(job.inputPath, job.requestedKind);
    const std::wstring stem = CleanStem(job.inputPath);

    ProcessSpec spec;

    if (kind == Kind::Video || kind == Kind::Image || kind == Kind::Audio) {
        spec.executable = FindTool(L"ffmpeg.exe");
        if (spec.executable.empty()) {
            throw std::runtime_error("FFmpeg was not found on PATH.");
        }
    }

    if (kind == Kind::Archive) {
        spec.executable = FindTool(L"tar.exe");
        if (spec.executable.empty()) {
            throw std::runtime_error("Windows tar.exe was not found.");
        }
    }

    if (kind == Kind::Video) {
        const int crf = job.quality == 0 ? 20 : (job.quality == 1 ? 24 : 28);
        const std::wstring preset = job.quality == 0 ? L"slow" : L"medium";
        const std::wstring audioBitrate = job.quality == 0 ? L"192k" : (job.quality == 1 ? L"160k" : L"128k");
        spec.outputPath = MakeUniqueOutputPath(job.outputFolder, stem, L".mp4");
        spec.description = L"Compressing video with H.265, " + QualityName(job.quality) + L" preset";
        spec.args = {
            L"-hide_banner",
            L"-nostdin",
            L"-y",
            L"-i", job.inputPath,
            L"-map", L"0:v:0",
            L"-map", L"0:a?",
            L"-c:v", L"libx265",
            L"-preset", preset,
            L"-crf", std::to_wstring(crf),
            L"-x265-params", L"log-level=error",
            L"-pix_fmt", L"yuv420p",
            L"-tag:v", L"hvc1",
            L"-c:a", L"aac",
            L"-b:a", audioBitrate,
            L"-movflags", L"+faststart",
            spec.outputPath
        };
        return spec;
    }

    if (kind == Kind::Image) {
        const std::wstring quality = job.quality == 0 ? L"90" : (job.quality == 1 ? L"82" : L"72");
        spec.outputPath = MakeUniqueOutputPath(job.outputFolder, stem, L".webp");
        spec.description = L"Compressing image as WebP, " + QualityName(job.quality) + L" preset";
        spec.args = {
            L"-hide_banner",
            L"-nostdin",
            L"-y",
            L"-i", job.inputPath,
            L"-frames:v", L"1",
            L"-c:v", L"libwebp",
            L"-q:v", quality,
            spec.outputPath
        };
        return spec;
    }

    if (kind == Kind::Audio) {
        const std::wstring bitrate = job.quality == 0 ? L"192k" : (job.quality == 1 ? L"160k" : L"96k");
        spec.outputPath = MakeUniqueOutputPath(job.outputFolder, stem, L".m4a");
        spec.description = L"Compressing audio as AAC, " + QualityName(job.quality) + L" preset";
        spec.args = {
            L"-hide_banner",
            L"-nostdin",
            L"-y",
            L"-i", job.inputPath,
            L"-vn",
            L"-c:a", L"aac",
            L"-b:a", bitrate,
            spec.outputPath
        };
        return spec;
    }

    fs::path input(job.inputPath);
    spec.outputPath = MakeUniqueOutputPath(job.outputFolder, stem, L".zip");
    spec.description = L"Creating lossless ZIP archive";
    spec.args = {
        L"-a",
        L"-cf",
        spec.outputPath,
        L"-C",
        input.parent_path().wstring(),
        input.filename().wstring()
    };
    return spec;
}

bool RunProcess(const ProcessSpec& spec, DWORD& exitCode) {
    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        PostLog(L"Could not create process pipe: " + FormatWindowsError(GetLastError()) + L"\r\n");
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process = {};
    std::wstring commandLine = BuildCommandLine(spec.executable, spec.args);

    PostLog(L"\r\n" + spec.description + L"\r\n");
    PostLog(L"Output: " + spec.outputPath + L"\r\n\r\n");

    const BOOL started = CreateProcessW(
        spec.executable.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);

    CloseHandle(writePipe);

    if (!started) {
        PostLog(L"Could not start process: " + FormatWindowsError(GetLastError()) + L"\r\n");
        CloseHandle(readPipe);
        return false;
    }

    char buffer[4096] = {};
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        PostLog(BytesToWide(buffer, read));
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(readPipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    return exitCode == 0;
}

void EnableInputs(bool enabled) {
    auto enable = [enabled](HWND hwnd) {
        if (hwnd) {
            EnableWindow(hwnd, enabled);
        }
    };

    enable(g_app.inputEdit);
    enable(g_app.outputEdit);
    enable(g_app.kindCombo);
    enable(g_app.qualityCombo);
    enable(g_app.browseFileButton);
    enable(g_app.browseFolderButton);
    enable(g_app.browseOutputButton);
    enable(g_app.centerAddButton);
    enable(g_app.startButton);

    if (g_app.openOutputButton) {
        EnableWindow(g_app.openOutputButton, enabled && !g_app.lastOutputPath.empty());
    }
}

void FinishJob(bool success, const std::wstring& outputPath) {
    g_app.busy = false;
    g_app.lastOutputPath = success ? outputPath : L"";
    EnableInputs(true);
    SetText(g_app.startButton, L"Start All");
    SetText(g_app.statusText, success ? L"Done" : L"Failed");
    EnableWindow(g_app.openOutputButton, success);
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
}

void WorkerThread(Job job) {
    bool success = false;
    std::wstring outputPath;

    try {
        const Kind kind = DetectKind(job.inputPath, job.requestedKind);
        PostLog(L"Input: " + job.inputPath + L"\r\n");
        PostLog(L"Mode: " + KindName(kind) + L"\r\n");
        if (kind != Kind::Archive) {
            PostLog(L"Quality: " + QualityName(job.quality) + L"\r\n");
        } else {
            PostLog(L"ZIP is lossless. Media files may already be compressed and might not shrink much.\r\n");
        }

        ProcessSpec spec = BuildProcessSpec(job);
        outputPath = spec.outputPath;
        DWORD exitCode = 0;
        success = RunProcess(spec, exitCode);

        if (!success) {
            PostLog(L"\r\nProcess failed with exit code " + std::to_wstring(exitCode) + L".\r\n");
        } else {
            const uintmax_t before = PathSize(fs::path(job.inputPath));
            const uintmax_t after = PathSize(fs::path(outputPath));
            PostLog(L"\r\nFinished successfully.\r\n");
            if (before > 0 && after > 0) {
                PostLog(L"Original: " + FormatBytes(before) + L"\r\n");
                PostLog(L"Compressed: " + FormatBytes(after) + L"\r\n");
                if (after < before) {
                    const double saved = (1.0 - (static_cast<double>(after) / static_cast<double>(before))) * 100.0;
                    std::wstringstream stream;
                    stream.setf(std::ios::fixed);
                    stream.precision(1);
                    stream << L"Saved: " << saved << L"%\r\n";
                    PostLog(stream.str());
                } else {
                    PostLog(L"The output is larger. That can happen when the original was already compressed.\r\n");
                }
            }
        }
    } catch (const std::exception& ex) {
        std::string message = ex.what();
        PostLog(L"\r\nError: " + std::wstring(message.begin(), message.end()) + L"\r\n");
        success = false;
    }

    PostMessageW(g_app.hwnd, WM_APP_DONE, success ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(outputPath)));
}

std::wstring PickFile(HWND owner) {
    wchar_t fileName[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileName));
    ofn.lpstrFilter =
        L"Supported files\0*.mp4;*.mov;*.mkv;*.avi;*.wmv;*.webm;*.m4v;*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.gif;*.mp3;*.wav;*.flac;*.aac;*.m4a;*.ogg;*.opus;*.zip;*.7z;*.rar;*.txt;*.pdf;*.docx;*.xlsx\0"
        L"All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        return fileName;
    }
    return L"";
}

std::wstring PickFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return L"";
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    std::wstring result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

void SetDefaultOutputFolder(const std::wstring& inputPath) {
    if (!Trim(GetText(g_app.outputEdit)).empty()) {
        return;
    }

    const std::wstring parent = ParentFolderFor(inputPath);
    if (!parent.empty()) {
        SetText(g_app.outputEdit, parent);
    }
}

std::wstring InputSummary(const std::wstring& inputPath) {
    if (inputPath.empty()) {
        return L"No file selected";
    }

    fs::path path(inputPath);
    std::wstring name = path.filename().wstring();
    if (name.empty()) {
        name = inputPath;
    }

    return name + L"  |  " + KindName(DetectKind(inputPath, 0));
}

void SetSelectedInput(const std::wstring& path) {
    if (path.empty()) {
        return;
    }

    SetText(g_app.inputEdit, path);
    SetDefaultOutputFolder(path);
    SetText(GetDlgItem(g_app.hwnd, ID_LABEL_SELECTED), InputSummary(path));
    SetText(g_app.statusText, L"Ready");
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
}

void ChooseInputFile() {
    const std::wstring path = PickFile(g_app.hwnd);
    if (!path.empty()) {
        SetSelectedInput(path);
    }
}

void ChooseInputFolder() {
    const std::wstring path = PickFolder(g_app.hwnd);
    if (!path.empty()) {
        SetSelectedInput(path);
    }
}

void ShowAddMenu(HWND anchor) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        ChooseInputFile();
        return;
    }

    AppendMenuW(menu, MF_STRING, IDC_ADD_FILE_MENU, L"Add Files");
    AppendMenuW(menu, MF_STRING, IDC_ADD_FOLDER_MENU, L"Add Folder");

    RECT anchorRect = {};
    GetWindowRect(anchor ? anchor : g_app.hwnd, &anchorRect);

    TrackPopupMenu(
        menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        anchorRect.left,
        anchorRect.bottom + 4,
        0,
        g_app.hwnd,
        nullptr);

    DestroyMenu(menu);
}

void StartCompression() {
    if (g_app.busy) {
        return;
    }

    Job job;
    job.inputPath = Trim(GetText(g_app.inputEdit));
    job.outputFolder = Trim(GetText(g_app.outputEdit));
    job.requestedKind = static_cast<int>(SendMessageW(g_app.kindCombo, CB_GETCURSEL, 0, 0));
    job.quality = static_cast<int>(SendMessageW(g_app.qualityCombo, CB_GETCURSEL, 0, 0));

    if (job.requestedKind < 0) {
        job.requestedKind = 0;
    }
    if (job.quality < 0) {
        job.quality = 0;
    }

    if (job.inputPath.empty() || !PathExists(job.inputPath)) {
        MessageBoxW(g_app.hwnd, L"Choose an input file or folder first.", L"Missing input", MB_ICONWARNING);
        return;
    }

    if (job.outputFolder.empty()) {
        job.outputFolder = ParentFolderFor(job.inputPath);
        SetText(g_app.outputEdit, job.outputFolder);
    }

    if (job.outputFolder.empty() || !IsDirectoryPath(job.outputFolder)) {
        MessageBoxW(g_app.hwnd, L"Choose a valid output folder.", L"Missing output folder", MB_ICONWARNING);
        return;
    }

    SetText(g_app.logEdit, L"");
    SetText(g_app.statusText, L"Working...");
    SetText(g_app.startButton, L"Working...");
    g_app.lastOutputPath.clear();
    g_app.busy = true;
    EnableInputs(false);

    std::thread(WorkerThread, job).detach();
}

HWND MakeControl(
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    DWORD exStyle,
    HWND parent,
    int id) {
    HWND hwnd = CreateWindowExW(
        exStyle,
        className,
        text,
        style,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr),
        nullptr);

    if (hwnd && g_app.font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
    }
    return hwnd;
}

void FillRectColor(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void DrawRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF stroke, int radius, int penStyle = PS_SOLID) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(penStyle, 1, stroke);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextInRect(HDC hdc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = nullptr;
    if (font) {
        oldFont = SelectObject(hdc, font);
    }
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
    if (oldFont) {
        SelectObject(hdc, oldFont);
    }
}

void DrawNavIcon(HDC hdc, int x, int y, bool selected) {
    COLORREF color = selected ? RGB(112, 70, 245) : RGB(134, 143, 158);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + 11, y + 11);
    MoveToEx(hdc, x + 3, y + 5, nullptr);
    LineTo(hdc, x + 8, y + 5);
    MoveToEx(hdc, x + 5, y + 3, nullptr);
    LineTo(hdc, x + 5, y + 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void PaintApp(HWND hwnd) {
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rect = {};
    GetClientRect(hwnd, &rect);

    const int sidebarW = 168;
    const COLORREF background = RGB(249, 250, 254);
    const COLORREF sidebar = RGB(243, 246, 252);
    const COLORREF border = RGB(228, 232, 241);
    const COLORREF accent = RGB(125, 83, 246);
    const COLORREF accentSoft = RGB(244, 239, 255);
    const COLORREF text = RGB(36, 42, 56);
    const COLORREF muted = RGB(117, 126, 142);

    FillRectColor(hdc, rect, background);
    RECT sidebarRect = { 0, 0, sidebarW, rect.bottom };
    FillRectColor(hdc, sidebarRect, sidebar);
    FillRectColor(hdc, { sidebarW - 1, 0, sidebarW, rect.bottom }, border);

    RECT logo = { 15, 10, 34, 29 };
    DrawRoundRect(hdc, logo, accent, RGB(102, 64, 230), 6);
    POINT play[] = { { 22, 15 }, { 22, 24 }, { 29, 19 } };
    HBRUSH playBrush = CreateSolidBrush(RGB(252, 176, 64));
    HGDIOBJ oldBrush = SelectObject(hdc, playBrush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, play, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(playBrush);

    if (g_app.selectedNavRect.bottom > 0) {
        DrawRoundRect(hdc, g_app.selectedNavRect, RGB(255, 255, 255), RGB(234, 237, 246), 0);
        RECT stripe = { g_app.selectedNavRect.left, g_app.selectedNavRect.top, g_app.selectedNavRect.left + 4, g_app.selectedNavRect.bottom };
        FillRectColor(hdc, stripe, accent);
    }

    const int navIconX = 22;
    const int navStartY = 58;
    const int navStep = 31;
    for (int i = 0; i < 9; ++i) {
        DrawNavIcon(hdc, navIconX, navStartY + i * navStep + 2, i == 3);
    }

    RECT topLine = { sidebarW, 38, rect.right - 12, 39 };
    FillRectColor(hdc, topLine, RGB(239, 242, 248));

    if (g_app.tabRect.bottom > 0) {
        DrawRoundRect(hdc, g_app.tabRect, RGB(235, 238, 247), RGB(225, 229, 239), 18);
        RECT active = g_app.tabRect;
        active.right = active.left + (active.right - active.left) / 2;
        DrawRoundRect(hdc, active, RGB(255, 255, 255), RGB(225, 229, 239), 18);
    }

    if (g_app.dropRect.bottom > 0) {
        DrawRoundRect(hdc, g_app.dropRect, RGB(255, 255, 255), border, 10, PS_DOT);

        const int centerX = (g_app.dropRect.left + g_app.dropRect.right) / 2;
        RECT sheet1 = { centerX - 48, g_app.dropRect.top + 43, centerX - 10, g_app.dropRect.top + 75 };
        RECT sheet2 = { centerX + 12, g_app.dropRect.top + 43, centerX + 50, g_app.dropRect.top + 75 };
        DrawRoundRect(hdc, sheet1, RGB(238, 241, 248), RGB(238, 241, 248), 4);
        DrawRoundRect(hdc, sheet2, RGB(238, 241, 248), RGB(238, 241, 248), 4);
    }

    RECT bottomBar = { sidebarW, rect.bottom - 96, rect.right, rect.bottom };
    FillRectColor(hdc, bottomBar, RGB(255, 255, 255));
    FillRectColor(hdc, { sidebarW, rect.bottom - 96, rect.right, rect.bottom - 95 }, RGB(236, 239, 247));

    RECT statusHint = { sidebarW + 18, rect.bottom - 26, rect.right - 285, rect.bottom - 6 };
    DrawTextInRect(hdc, L"Tip: use 70% for near-transparent quality.", statusHint, g_app.font, muted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    EndPaint(hwnd, &ps);
}

void Layout(HWND hwnd) {
    RECT rect = {};
    GetClientRect(hwnd, &rect);

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int sidebarW = 168;
    const int contentX = sidebarW;
    const int contentW = std::max(360, width - sidebarW);
    const int margin = 18;
    const int gap = 8;
    const int fieldH = 28;

    auto move = [](HWND control, int x, int yPos, int w, int h) {
        if (control) {
            MoveWindow(control, x, yPos, w, h, TRUE);
        }
    };

    ShowWindow(g_app.inputEdit, SW_HIDE);
    ShowWindow(g_app.kindCombo, SW_HIDE);
    ShowWindow(g_app.browseFolderButton, SW_HIDE);

    move(g_app.inputEdit, 0, 0, 1, 1);
    move(g_app.kindCombo, 0, 0, 1, 1);
    move(g_app.browseFolderButton, 0, 0, 1, 1);

    move(GetDlgItem(hwnd, ID_LABEL_APP_TITLE), 44, 11, 118, 20);

    const int navX = 44;
    const int navY = 58;
    const int navStep = 31;
    const int navW = 118;
    const int navH = 20;
    move(GetDlgItem(hwnd, ID_LABEL_NAV_HOME), navX, navY, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_CONVERTER), navX, navY + navStep, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_DOWNLOADER), navX, navY + navStep * 2, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_VIDEO_COMPRESSOR), navX, navY + navStep * 3, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_VIDEO_EDITOR), navX, navY + navStep * 4, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_MERGER), navX, navY + navStep * 5, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_SCREEN_RECORDER), navX, navY + navStep * 6, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_DVD_BURNER), navX, navY + navStep * 7, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_PLAYER), navX, navY + navStep * 8, navW, navH);
    move(GetDlgItem(hwnd, ID_LABEL_NAV_TOOLBOX), navX, navY + navStep * 9, navW, navH);
    g_app.selectedNavRect = { 0, navY + navStep * 3 - 7, sidebarW - 6, navY + navStep * 3 + 25 };

    move(g_app.browseFileButton, contentX + margin, 42, 116, 32);

    const int tabW = 184;
    const int tabH = 24;
    const int tabX = contentX + (contentW - tabW) / 2;
    g_app.tabRect = { tabX, 48, tabX + tabW, 48 + tabH };
    move(GetDlgItem(hwnd, ID_LABEL_TAB_COMPRESSING), tabX + 3, 51, tabW / 2 - 6, 18);
    move(GetDlgItem(hwnd, ID_LABEL_TAB_FINISHED), tabX + tabW / 2 + 3, 51, tabW / 2 - 6, 18);

    int panelW = std::min(520, std::max(390, contentW - 180));
    int panelH = std::min(285, std::max(230, height - 300));
    const int panelX = contentX + (contentW - panelW) / 2;
    const int panelY = 145;
    if (panelY + panelH > height - 112) {
        panelH = std::max(210, height - panelY - 112);
    }
    g_app.dropRect = { panelX, panelY, panelX + panelW, panelY + panelH };

    move(g_app.centerAddButton, panelX + panelW / 2 - 28, panelY + 54, 56, 40);
    move(GetDlgItem(hwnd, ID_LABEL_DROP_TITLE), panelX + 40, panelY + 113, panelW - 80, 24);
    move(GetDlgItem(hwnd, ID_LABEL_SELECTED), panelX + 30, panelY + 140, panelW - 60, 24);

    const int logY = panelY + 174;
    move(g_app.logEdit, panelX + 24, logY, panelW - 48, std::max(56, panelY + panelH - logY - 16));

    const int bottomTop = height - 82;
    move(GetDlgItem(hwnd, ID_LABEL_SIZE), contentX + margin, bottomTop, 62, 20);
    move(g_app.qualityCombo, contentX + 80, bottomTop - 2, 165, 160);

    move(GetDlgItem(hwnd, ID_LABEL_LOCATION), contentX + margin, bottomTop + 31, 82, 20);
    const int startW = 110;
    const int outputButtonW = 34;
    const int openW = 108;
    const int startX = width - margin - startW;
    const int openX = startX - gap - openW;
    const int outputX = contentX + 104;
    const int outputW = std::max(150, openX - gap - outputButtonW - gap - outputX);
    move(g_app.outputEdit, outputX, bottomTop + 27, outputW, fieldH);
    move(g_app.browseOutputButton, outputX + outputW + gap, bottomTop + 27, outputButtonW, fieldH);
    move(g_app.openOutputButton, openX, bottomTop + 25, openW, 32);
    move(g_app.startButton, startX, bottomTop + 25, startW, 32);
    move(g_app.statusText, contentX + 254, bottomTop, std::max(120, openX - contentX - 270), 20);

    InvalidateRect(hwnd, nullptr, TRUE);
}

void AddComboItem(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

void SetControlFont(HWND hwnd, HFONT font) {
    if (hwnd && font) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

HWND CreateLabel(HWND parent, int id, const wchar_t* text, bool bold = false) {
    HWND label = MakeControl(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, parent, id);
    SetControlFont(label, bold ? g_app.boldFont : g_app.font);
    return label;
}

void CreateUi(HWND hwnd) {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);

    LOGFONTW regular = metrics.lfMessageFont;
    g_app.font = CreateFontIndirectW(&regular);

    LOGFONTW bold = regular;
    bold.lfWeight = FW_SEMIBOLD;
    g_app.boldFont = CreateFontIndirectW(&bold);

    LOGFONTW title = regular;
    title.lfWeight = FW_SEMIBOLD;
    title.lfHeight = -14;
    g_app.titleFont = CreateFontIndirectW(&title);

    CreateLabel(hwnd, ID_LABEL_APP_TITLE, L"File Compressor", true);
    SetControlFont(GetDlgItem(hwnd, ID_LABEL_APP_TITLE), g_app.titleFont);
    CreateLabel(hwnd, ID_LABEL_NAV_HOME, L"Home");
    CreateLabel(hwnd, ID_LABEL_NAV_CONVERTER, L"Converter");
    CreateLabel(hwnd, ID_LABEL_NAV_DOWNLOADER, L"Downloader");
    CreateLabel(hwnd, ID_LABEL_NAV_VIDEO_COMPRESSOR, L"Video Compressor", true);
    CreateLabel(hwnd, ID_LABEL_NAV_VIDEO_EDITOR, L"Video Editor");
    CreateLabel(hwnd, ID_LABEL_NAV_MERGER, L"Merger");
    CreateLabel(hwnd, ID_LABEL_NAV_SCREEN_RECORDER, L"Screen Recorder");
    CreateLabel(hwnd, ID_LABEL_NAV_DVD_BURNER, L"DVD Burner");
    CreateLabel(hwnd, ID_LABEL_NAV_PLAYER, L"Player");
    CreateLabel(hwnd, ID_LABEL_NAV_TOOLBOX, L"Toolbox");

    CreateLabel(hwnd, ID_LABEL_TAB_COMPRESSING, L"Compressing", true);
    CreateLabel(hwnd, ID_LABEL_TAB_FINISHED, L"Finished");
    CreateLabel(hwnd, ID_LABEL_DROP_TITLE, L"Add or drag files here to start compression", true);
    CreateLabel(hwnd, ID_LABEL_SELECTED, L"No file selected");
    CreateLabel(hwnd, ID_LABEL_SIZE, L"File Size");
    CreateLabel(hwnd, ID_LABEL_LOCATION, L"File Location");

    g_app.inputEdit = MakeControl(L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, hwnd, IDC_INPUT_EDIT);
    g_app.browseFileButton = MakeControl(L"BUTTON", L"Add Files v", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, hwnd, IDC_BROWSE_FILE);
    g_app.browseFolderButton = MakeControl(L"BUTTON", L"Add Folder", WS_CHILD, 0, hwnd, IDC_BROWSE_FOLDER);
    g_app.centerAddButton = MakeControl(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, hwnd, IDC_CENTER_ADD);

    g_app.outputEdit = MakeControl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, hwnd, IDC_OUTPUT_EDIT);
    g_app.browseOutputButton = MakeControl(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, hwnd, IDC_BROWSE_OUTPUT);

    g_app.kindCombo = MakeControl(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, hwnd, IDC_KIND_COMBO);
    AddComboItem(g_app.kindCombo, L"Auto");
    AddComboItem(g_app.kindCombo, L"Video");
    AddComboItem(g_app.kindCombo, L"Image");
    AddComboItem(g_app.kindCombo, L"Audio");
    AddComboItem(g_app.kindCombo, L"ZIP archive");
    SendMessageW(g_app.kindCombo, CB_SETCURSEL, 0, 0);

    g_app.qualityCombo = MakeControl(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, hwnd, IDC_QUALITY_COMBO);
    AddComboItem(g_app.qualityCombo, L"70% - near-transparent");
    AddComboItem(g_app.qualityCombo, L"50% - balanced");
    AddComboItem(g_app.qualityCombo, L"30% - small");
    SendMessageW(g_app.qualityCombo, CB_SETCURSEL, 0, 0);

    g_app.startButton = MakeControl(L"BUTTON", L"Start All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, hwnd, IDC_START);
    g_app.openOutputButton = MakeControl(L"BUTTON", L"Open Output", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, hwnd, IDC_OPEN_OUTPUT);
    EnableWindow(g_app.openOutputButton, FALSE);
    g_app.statusText = MakeControl(L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE, 0, hwnd, IDC_STATUS);
    SetControlFont(g_app.statusText, g_app.boldFont);

    g_app.logEdit = MakeControl(
        L"EDIT",
        L"Step 1: Add files or drag files here to start compression.\r\nStep 2: Choose file size and output folder.\r\nStep 3: Start compression.\r\n",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0,
        hwnd,
        IDC_LOG);

    Layout(hwnd);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        CreateUi(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintApp(hwnd);
        return 0;

    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BROWSE_FILE) {
            ShowAddMenu(g_app.browseFileButton);
            return 0;
        }
        if (id == IDC_BROWSE_FOLDER) {
            ChooseInputFolder();
            return 0;
        }
        if (id == IDC_CENTER_ADD) {
            ShowAddMenu(g_app.centerAddButton);
            return 0;
        }
        if (id == IDC_ADD_FILE_MENU) {
            ChooseInputFile();
            return 0;
        }
        if (id == IDC_ADD_FOLDER_MENU) {
            ChooseInputFolder();
            return 0;
        }
        if (id == IDC_BROWSE_OUTPUT) {
            const std::wstring path = PickFolder(hwnd);
            if (!path.empty()) {
                SetText(g_app.outputEdit, path);
            }
            return 0;
        }
        if (id == IDC_START) {
            StartCompression();
            return 0;
        }
        if (id == IDC_OPEN_OUTPUT) {
            if (!g_app.lastOutputPath.empty()) {
                ShellExecuteW(hwnd, L"open", L"explorer.exe", (L"/select,\"" + g_app.lastOutputPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0) {
            const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(static_cast<size_t>(length) + 1, L'\0');
            DragQueryFileW(drop, 0, path.data(), length + 1);
            path.resize(wcslen(path.c_str()));
            SetSelectedInput(path);
        }
        DragFinish(drop);
        return 0;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlID == IDC_CENTER_ADD) {
            const bool disabled = (item->itemState & ODS_DISABLED) != 0;
            const COLORREF fill = disabled ? RGB(190, 190, 196) : RGB(125, 83, 246);
            DrawRoundRect(item->hDC, item->rcItem, fill, fill, 8);
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
            HGDIOBJ oldPen = SelectObject(item->hDC, pen);
            const int cx = (item->rcItem.left + item->rcItem.right) / 2;
            const int cy = (item->rcItem.top + item->rcItem.bottom) / 2;
            MoveToEx(item->hDC, cx - 8, cy, nullptr);
            LineTo(item->hDC, cx + 9, cy);
            MoveToEx(item->hDC, cx, cy - 8, nullptr);
            LineTo(item->hDC, cx, cy + 9);
            SelectObject(item->hDC, oldPen);
            DeleteObject(pen);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(56, 63, 78));
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, RGB(255, 255, 255));
        SetTextColor(hdc, RGB(56, 63, 78));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_APP_LOG: {
        std::wstring* text = reinterpret_cast<std::wstring*>(lParam);
        if (text) {
            AppendLog(*text);
            delete text;
        }
        return 0;
    }

    case WM_APP_DONE: {
        std::wstring* outputPath = reinterpret_cast<std::wstring*>(lParam);
        FinishJob(wParam == 1, outputPath ? *outputPath : L"");
        delete outputPath;
        return 0;
    }

    case WM_CLOSE:
        if (g_app.busy) {
            const int choice = MessageBoxW(hwnd, L"Compression is still running. Close anyway?", L"File Compressor", MB_YESNO | MB_ICONQUESTION);
            if (choice != IDYES) {
                return 0;
            }
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        DragAcceptFiles(hwnd, FALSE);
        if (g_app.font) {
            DeleteObject(g_app.font);
            g_app.font = nullptr;
        }
        if (g_app.boldFont) {
            DeleteObject(g_app.boldFont);
            g_app.boldFont = nullptr;
        }
        if (g_app.titleFont) {
            DeleteObject(g_app.titleFont);
            g_app.titleFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    const wchar_t className[] = L"FileCompressorWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"File Compressor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        860,
        560,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
