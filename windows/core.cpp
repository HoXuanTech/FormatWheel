#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "core.h"
#include <windows.h>
#include <wincodec.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include <vector>

namespace fw {
namespace {

template<class T> class Com {
public:
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
    T** out() { return &value; }
    T* operator->() const { return value; }
    operator bool() const { return value != nullptr; }
};

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
}

std::wstring extension(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    return dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) ? L"" : lower(path.substr(dot + 1));
}

std::wstring stem(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    const size_t end = dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash) ? path.size() : dot;
    return path.substr(0, end);
}

std::wstring filename(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring unique_out(const std::wstring& input, const std::wstring& suffix, const std::wstring& ext) {
    const std::wstring base = stem(input) + suffix;
    for (int i = 1; i < 10000; ++i) {
        std::wstring out = base + (i == 1 ? L"" : L" " + std::to_wstring(i)) + L"." + ext;
        if (GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES) return out;
    }
    throw std::runtime_error("no free output path");
}

std::wstring executable_path(const std::wstring& name) {
    wchar_t module[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    const std::wstring local = std::wstring(module).substr(0, std::wstring(module).find_last_of(L"\\/")) + L"\\" + name;
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, found, nullptr)) return found;
    return L"";
}

std::wstring quoted(const std::wstring& arg) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result += L'"';
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

void run_tool(const std::wstring& name, const std::vector<std::wstring>& args) {
    const std::wstring exe = executable_path(name);
    if (exe.empty()) throw std::runtime_error("missing tool");
    std::wstring command = quoted(exe);
    for (const auto& arg : args) command += L" " + quoted(arg);
    STARTUPINFOW start = {};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESHOWWINDOW;
    start.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &process)) {
        throw std::runtime_error("tool launch failed");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) throw std::runtime_error("tool returned an error");
}

void copy_clipboard(const std::wstring& value) {
    if (!OpenClipboard(nullptr)) throw std::runtime_error("clipboard unavailable");
    EmptyClipboard();
    const size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) { CloseClipboard(); throw std::runtime_error("clipboard allocation failed"); }
    void* data = GlobalLock(memory);
    memcpy(data, value.c_str(), bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) { GlobalFree(memory); CloseClipboard(); throw std::runtime_error("clipboard write failed"); }
    CloseClipboard();
}

std::wstring sha256(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open file");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0, received = 0;
    std::wstring result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &received, 0) >= 0) {
        std::vector<unsigned char> object(object_size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0) {
            std::vector<unsigned char> data(1024 * 1024);
            DWORD count = 0;
            bool okay = true;
            while (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) && count) {
                if (BCryptHashData(hash, data.data(), count, 0) < 0) { okay = false; break; }
            }
            unsigned char digest[32] = {};
            if (okay && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
                const wchar_t* hex = L"0123456789abcdef";
                for (unsigned char b : digest) { result += hex[b >> 4]; result += hex[b & 15]; }
            }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (result.empty()) throw std::runtime_error("SHA-256 failed");
    return result;
}

void require(HRESULT hr) {
    if (FAILED(hr)) throw std::runtime_error("Windows image codec failed");
}

std::wstring image_wic(const std::wstring& input, const std::wstring& id) {
    const bool half = id == L"img.half";
    const bool mono = id == L"img.mono";
    const bool rotate = id == L"img.rotate";
    const bool strip = id == L"img.strip";
    const bool compress = id == L"img.compress";
    std::wstring ext = id.find(L"img.to.") == 0 ? id.substr(7) : extension(input);
    if (ext == L"jpeg") ext = L"jpg";
    GUID container = GUID_ContainerFormatPng;
    WICPixelFormatGUID pixel = GUID_WICPixelFormat32bppBGRA;
    if (ext == L"jpg") { container = GUID_ContainerFormatJpeg; pixel = GUID_WICPixelFormat24bppBGR; }
    else if (ext == L"tiff" || ext == L"tif") container = GUID_ContainerFormatTiff;
    else if (ext == L"bmp") container = GUID_ContainerFormatBmp;
    else if (ext != L"png") throw std::runtime_error("unsupported WIC format");
    const std::wstring out = unique_out(input, id.find(L"img.to.") == 0 ? L"" : std::wstring(L" ") + (strip ? L"去隱私" : compress ? L"壓縮" : half ? L"縮小" : mono ? L"黑白" : L"旋轉"), ext);

    Com<IWICImagingFactory> factory;
    require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.out())));
    Com<IWICBitmapDecoder> decoder;
    require(factory->CreateDecoderFromFilename(input.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.out()));
    Com<IWICBitmapFrameDecode> frame;
    require(decoder->GetFrame(0, frame.out()));
    IWICBitmapSource* source = frame.value;
    Com<IWICBitmapScaler> scaler;
    Com<IWICBitmapFlipRotator> rotator;
    Com<IWICFormatConverter> grayscale;
    UINT width = 0, height = 0;
    require(frame->GetSize(&width, &height));
    if (half) {
        width = std::max<UINT>(1, width / 2);
        height = std::max<UINT>(1, height / 2);
        require(factory->CreateBitmapScaler(scaler.out()));
        require(scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant));
        source = scaler.value;
    }
    if (rotate) {
        std::swap(width, height);
        require(factory->CreateBitmapFlipRotator(rotator.out()));
        require(rotator->Initialize(source, WICBitmapTransformRotate90));
        source = rotator.value;
    }
    if (mono) {
        require(factory->CreateFormatConverter(grayscale.out()));
        require(grayscale->Initialize(source, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
        source = grayscale.value;
    }
    Com<IWICFormatConverter> converted;
    require(factory->CreateFormatConverter(converted.out()));
    require(converted->Initialize(source, pixel, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
    Com<IWICStream> stream;
    require(factory->CreateStream(stream.out()));
    require(stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE));
    Com<IWICBitmapEncoder> encoder;
    require(factory->CreateEncoder(container, nullptr, encoder.out()));
    require(encoder->Initialize(stream.value, WICBitmapEncoderNoCache));
    Com<IWICBitmapFrameEncode> out_frame;
    Com<IPropertyBag2> properties;
    require(encoder->CreateNewFrame(out_frame.out(), properties.out()));
    if (ext == L"jpg") {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT quality;
        VariantInit(&quality);
        quality.vt = VT_R4;
        quality.fltVal = compress ? 0.72f : 0.92f;
        properties->Write(1, &option, &quality);
    }
    require(out_frame->Initialize(properties.value));
    require(out_frame->SetSize(width, height));
    require(out_frame->SetPixelFormat(&pixel));
    require(out_frame->WriteSource(converted.value, nullptr));
    require(out_frame->Commit());
    require(encoder->Commit());
    return out;
}

std::wstring ffmpeg(const std::wstring& input, const std::wstring& suffix, const std::wstring& ext,
                    const std::vector<std::wstring>& options) {
    if (!tool_available(L"ffmpeg.exe")) throw std::runtime_error("ffmpeg missing");
    const std::wstring out = unique_out(input, suffix, ext);
    std::vector<std::wstring> args = {L"-hide_banner", L"-loglevel", L"error", L"-nostdin", L"-n", L"-i", input};
    args.insert(args.end(), options.begin(), options.end());
    args.push_back(out);
    run_tool(L"ffmpeg.exe", args);
    return out;
}

} // namespace

bool tool_available(const std::wstring& executable) { return !executable_path(executable).empty(); }

std::wstring unavailable_reason(const std::wstring& id) {
    if (id == L"gen.path" || id == L"gen.name" || id == L"gen.sha") return L"";
    if (id == L"gen.zip") return tool_available(L"7z.exe") ? L"" : L"尚未安裝 7-Zip";
    if (id == L"img.to.png" || id == L"img.to.jpg" || id == L"img.to.tiff" ||
        id == L"img.compress" || id == L"img.half" || id == L"img.mono" ||
        id == L"img.rotate" || id == L"img.strip") return L"";
    if (id == L"img.to.webp" || id == L"img.to.avif" || id == L"img.to.gif" || id == L"img.to.heic" ||
        id.find(L"vid.") == 0 || id.find(L"aud.") == 0)
        return tool_available(L"ffmpeg.exe") ? L"" : L"尚未安裝 FFmpeg";
    if (id == L"pdf.to.png" || id == L"pdf.to.jpg") return tool_available(L"pdftoppm.exe") ? L"" : L"尚未安裝 Poppler";
    if (id == L"pdf.to.txt") return tool_available(L"pdftotext.exe") ? L"" : L"尚未安裝 Poppler";
    if (id == L"pdf.split") return tool_available(L"pdfseparate.exe") ? L"" : L"尚未安裝 Poppler";
    if (id == L"pdf.merge") return tool_available(L"pdfunite.exe") ? L"" : L"尚未安裝 Poppler";
    return L"Windows 版尚未完成這項功能";
}

Kind kind_of(const std::wstring& path) {
    const auto ext = extension(path);
    if (ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"bmp" || ext == L"gif" || ext == L"tif" || ext == L"tiff" || ext == L"webp" || ext == L"heic" || ext == L"avif") return Kind::image;
    if (ext == L"pdf") return Kind::pdf;
    if (ext == L"mp4" || ext == L"mov" || ext == L"mkv" || ext == L"webm" || ext == L"avi" || ext == L"m4v") return Kind::video;
    if (ext == L"mp3" || ext == L"m4a" || ext == L"wav" || ext == L"flac" || ext == L"aiff" || ext == L"opus" || ext == L"aac") return Kind::audio;
    if (ext == L"docx" || ext == L"doc" || ext == L"txt" || ext == L"md" || ext == L"rtf" || ext == L"odt" || ext == L"html") return Kind::document;
    return Kind::other;
}

const wchar_t* kind_label(Kind kind) {
    switch (kind) {
        case Kind::image: return L"圖片";
        case Kind::pdf: return L"PDF";
        case Kind::video: return L"影片";
        case Kind::audio: return L"音訊";
        case Kind::document: return L"文件";
        default: return L"檔案";
    }
}

std::vector<Action> actions(Kind kind, int page) {
    if (kind == Kind::image) return page == 0 ? std::vector<Action>{
        {L"img.to.png",L"PNG",L"保留透明"},{L"img.to.jpg",L"JPG",L"照片常用"},{L"img.to.heic",L"HEIC",L"需要 ffmpeg"},{L"img.to.webp",L"WebP",L"需要 ffmpeg"},
        {L"img.to.pdf",L"PDF",L"尚未支援"},{L"img.to.avif",L"AVIF",L"需要 ffmpeg"},{L"img.to.gif",L"GIF",L"需要 ffmpeg"},{L"img.to.tiff",L"TIFF",L"印刷常用"}
    } : std::vector<Action>{
        {L"img.compress",L"壓縮",L"縮小檔案"},{L"img.half",L"縮小一半",L"長寬減半"},{L"img.cutout",L"去背",L"尚未支援"},{L"img.ocr",L"文字辨識",L"尚未支援"},
        {L"img.mono",L"黑白",L"轉成灰階"},{L"img.rotate",L"旋轉",L"順時針 90°"},{L"img.strip",L"去隱私",L"重新編碼移除中繼資料"},{L"img.backdrop",L"加背景",L"尚未支援"}
    };
    if (kind == Kind::video) return page == 0 ? std::vector<Action>{
        {L"vid.to.mp4",L"MP4",L"通用格式"},{L"vid.to.mov",L"MOV",L"剪輯常用"},{L"vid.to.hevc",L"HEVC",L"H.265"},{L"vid.to.webm",L"WebM",L"網頁影片"},
        {L"vid.to.gif",L"GIF",L"動圖"},{L"vid.to.mp3",L"MP3",L"提取音訊"},{L"vid.to.m4a",L"M4A",L"提取音訊"},{L"vid.to.wav",L"WAV",L"無損音訊"}
    } : std::vector<Action>{
        {L"vid.compress",L"壓縮",L"縮小影片"},{L"vid.mute",L"靜音",L"移除聲音"},{L"vid.720p",L"720P",L"縮小畫面"},{L"vid.cover",L"截封面",L"擷取首格"},
        {L"vid.fast",L"2 倍速",L"加速"},{L"vid.slow",L"慢放",L"半速"},{L"vid.strip",L"去隱私",L"移除中繼資料"},{L"gen.sha",L"SHA-256",L"複製雜湊值"}
    };
    if (kind == Kind::audio) return page == 0 ? std::vector<Action>{
        {L"aud.to.mp3",L"MP3",L"通用格式"},{L"aud.to.m4a",L"M4A",L"AAC"},{L"aud.to.wav",L"WAV",L"無損"},{L"aud.to.flac",L"FLAC",L"無損壓縮"},
        {L"aud.to.aiff",L"AIFF",L"無損"},{L"aud.to.opus",L"Opus",L"語音常用"},{L"aud.to.alac",L"ALAC",L"蘋果無損"},{L"aud.to.m4r",L"鈴聲",L"iPhone 鈴聲"}
    } : std::vector<Action>{
        {L"aud.compress",L"壓縮",L"縮小檔案"},{L"aud.mono",L"單聲道",L"左右聲道合併"},{L"aud.loud",L"響度統一",L"音量標準化"},{L"aud.fast",L"1.5 倍速",L"加速"},
        {L"gen.sha",L"SHA-256",L"複製雜湊值"},{L"gen.path",L"複製路徑",L"複製完整路徑"},{L"gen.name",L"複製檔名",L"複製檔名"},{L"gen.zip",L"ZIP",L"需要 7-Zip"}
    };
    if (kind == Kind::pdf) return page == 0 ? std::vector<Action>{
        {L"pdf.to.png",L"PNG",L"需要 pdftoppm"},{L"pdf.to.jpg",L"JPG",L"需要 pdftoppm"},{L"pdf.to.long",L"長圖",L"尚未支援"},{L"pdf.to.docx",L"Word",L"尚未支援"},
        {L"pdf.to.txt",L"TXT",L"需要 pdftotext"},{L"pdf.to.md",L"Markdown",L"尚未支援"},{L"pdf.to.rtf",L"RTF",L"尚未支援"},{L"pdf.to.html",L"HTML",L"尚未支援"}
    } : std::vector<Action>{
        {L"pdf.compress",L"壓縮",L"尚未支援"},{L"pdf.split",L"拆頁",L"需要 pdfseparate"},{L"pdf.merge",L"合併",L"需要 pdfunite"},{L"pdf.rotate",L"旋轉",L"尚未支援"},
        {L"pdf.mono",L"黑白",L"尚未支援"},{L"pdf.ocr",L"文字辨識",L"尚未支援"},{L"pdf.strip",L"去隱私",L"尚未支援"},{L"gen.sha",L"SHA-256",L"複製雜湊值"}
    };
    if (kind == Kind::document) return page == 0 ? std::vector<Action>{
        {L"doc.to.pdf",L"PDF",L"需要 LibreOffice"},{L"doc.to.docx",L"Word",L"需要 LibreOffice"},{L"doc.to.txt",L"TXT",L"需要 LibreOffice"},{L"doc.to.md",L"Markdown",L"尚未支援"},
        {L"doc.to.html",L"HTML",L"需要 LibreOffice"},{L"doc.to.rtf",L"RTF",L"需要 LibreOffice"},{L"doc.to.odt",L"ODT",L"需要 LibreOffice"},{L"doc.to.doc",L"DOC",L"需要 LibreOffice"}
    } : std::vector<Action>{
        {L"doc.count",L"字數統計",L"文字檔可用"},{L"gen.path",L"複製路徑",L"複製完整路徑"},{L"doc.copy",L"複製全文",L"文字檔可用"},{L"gen.name",L"複製檔名",L"複製檔名"},
        {L"doc.strip",L"去隱私",L"尚未支援"},{L"gen.sha",L"SHA-256",L"複製雜湊值"},{L"gen.zip",L"ZIP",L"需要 7-Zip"},{L"gen.path",L"路徑",L"複製完整路徑"}
    };
    return {{L"gen.zip",L"ZIP",L"需要 7-Zip"},{L"gen.path",L"複製路徑",L"複製完整路徑"},{L"gen.name",L"複製檔名",L"複製檔名"},{L"gen.sha",L"SHA-256",L"複製雜湊值"}};
}

std::wstring run(const std::wstring& id, const std::vector<std::wstring>& files) {
    if (files.empty()) throw std::runtime_error("no input files");
    const std::wstring& input = files.front();
    if (id == L"gen.path" || id == L"gen.name") {
        std::wstring value;
        for (const auto& file : files) { if (!value.empty()) value += L"\r\n"; value += id == L"gen.name" ? filename(file) : file; }
        copy_clipboard(value);
        return L"已複製到剪貼簿";
    }
    if (id == L"gen.sha") { copy_clipboard(sha256(input)); return L"SHA-256 已複製到剪貼簿"; }
    if (id == L"gen.zip") {
        if (!tool_available(L"7z.exe")) throw std::runtime_error("7-Zip missing");
        const std::wstring out = unique_out(input, L" 封存", L"zip");
        std::vector<std::wstring> args = {L"a", L"-tzip", out};
        args.insert(args.end(), files.begin(), files.end());
        run_tool(L"7z.exe", args);
        return out;
    }
    if (id == L"img.to.png" || id == L"img.to.jpg" || id == L"img.to.tiff" || id == L"img.compress" || id == L"img.half" || id == L"img.mono" || id == L"img.rotate" || id == L"img.strip") return image_wic(input, id);
    if (id == L"img.to.webp" || id == L"img.to.avif" || id == L"img.to.gif" || id == L"img.to.heic") return ffmpeg(input, L"", id.substr(7), {L"-frames:v",L"1"});
    if (id == L"pdf.to.png" || id == L"pdf.to.jpg") {
        const bool png = id == L"pdf.to.png";
        const std::wstring prefix = stem(input) + (png ? L" PNG" : L" JPG");
        run_tool(L"pdftoppm.exe", {L"-r", L"150", png ? L"-png" : L"-jpeg", input, prefix});
        return prefix + L"-1." + (png ? L"png" : L"jpg");
    }
    if (id == L"pdf.to.txt") {
        const std::wstring out = unique_out(input, L"", L"txt");
        run_tool(L"pdftotext.exe", {L"-layout", input, out});
        return out;
    }
    if (id == L"pdf.split") {
        const std::wstring pattern = stem(input) + L"-%d.pdf";
        run_tool(L"pdfseparate.exe", {input, pattern});
        return pattern;
    }
    if (id == L"pdf.merge") {
        if (files.size() < 2) throw std::runtime_error("need two PDF files");
        const std::wstring out = unique_out(input, L" 合併", L"pdf");
        std::vector<std::wstring> args = files;
        args.push_back(out);
        run_tool(L"pdfunite.exe", args);
        return out;
    }
    if (id.find(L"vid.") == 0 || id.find(L"aud.") == 0) {
        std::wstring ext, suffix;
        std::vector<std::wstring> options;
        if (id == L"vid.to.mp4") { ext=L"mp4"; options={L"-c:v",L"mpeg4",L"-q:v",L"4",L"-c:a",L"aac"}; }
        else if (id == L"vid.to.mov") { ext=L"mov"; options={L"-c:v",L"mpeg4",L"-q:v",L"4",L"-c:a",L"aac"}; }
        else if (id == L"vid.to.hevc") { ext=L"mp4"; suffix=L" HEVC"; options={L"-c:v",L"libx265",L"-crf",L"28",L"-c:a",L"aac"}; }
        else if (id == L"vid.to.webm") { ext=L"webm"; options={L"-c:v",L"libvpx-vp9",L"-b:v",L"0",L"-crf",L"35",L"-c:a",L"libopus"}; }
        else if (id == L"vid.to.gif") { ext=L"gif"; options={L"-t",L"30",L"-vf",L"fps=12,scale=640:-1:flags=lanczos"}; }
        else if (id == L"vid.to.mp3" || id == L"aud.to.mp3") { ext=L"mp3"; options={L"-vn",L"-c:a",L"libmp3lame",L"-q:a",L"3"}; }
        else if (id == L"vid.to.m4a" || id == L"aud.to.m4a") { ext=L"m4a"; options={L"-vn",L"-c:a",L"aac",L"-b:a",L"192k"}; }
        else if (id == L"vid.to.wav" || id == L"aud.to.wav") { ext=L"wav"; options={L"-vn",L"-c:a",L"pcm_s16le"}; }
        else if (id == L"aud.to.flac") { ext=L"flac"; options={L"-c:a",L"flac"}; }
        else if (id == L"aud.to.aiff") { ext=L"aiff"; options={L"-c:a",L"pcm_s16be"}; }
        else if (id == L"aud.to.opus") { ext=L"opus"; options={L"-c:a",L"libopus",L"-b:a",L"96k"}; }
        else if (id == L"aud.to.alac") { ext=L"m4a"; suffix=L" ALAC"; options={L"-c:a",L"alac"}; }
        else if (id == L"aud.to.m4r") { ext=L"m4r"; options={L"-t",L"30",L"-c:a",L"aac",L"-b:a",L"192k"}; }
        else if (id == L"vid.compress") { ext=L"mp4"; suffix=L" 壓縮"; options={L"-c:v",L"mpeg4",L"-b:v",L"1200k",L"-c:a",L"aac",L"-b:a",L"128k"}; }
        else if (id == L"vid.mute") { ext=L"mp4"; suffix=L" 靜音"; options={L"-an",L"-c:v",L"copy"}; }
        else if (id == L"vid.720p") { ext=L"mp4"; suffix=L" 720P"; options={L"-vf",L"scale=-2:720",L"-c:v",L"mpeg4",L"-c:a",L"aac"}; }
        else if (id == L"vid.cover") { ext=L"png"; suffix=L" 封面"; options={L"-frames:v",L"1"}; }
        else if (id == L"vid.fast") { ext=L"mp4"; suffix=L" 2倍速"; options={L"-filter:v",L"setpts=0.5*PTS",L"-filter:a",L"atempo=2.0"}; }
        else if (id == L"vid.slow") { ext=L"mp4"; suffix=L" 慢放"; options={L"-filter:v",L"setpts=2.0*PTS",L"-filter:a",L"atempo=0.5"}; }
        else if (id == L"vid.strip") { ext=extension(input); suffix=L" 去隱私"; options={L"-map_metadata",L"-1",L"-c",L"copy"}; }
        else if (id == L"aud.compress") { ext=L"m4a"; suffix=L" 壓縮"; options={L"-c:a",L"aac",L"-b:a",L"96k"}; }
        else if (id == L"aud.mono") { ext=L"m4a"; suffix=L" 單聲道"; options={L"-ac",L"1",L"-c:a",L"aac"}; }
        else if (id == L"aud.loud") { ext=L"m4a"; suffix=L" 響度統一"; options={L"-af",L"loudnorm",L"-c:a",L"aac"}; }
        else if (id == L"aud.fast") { ext=L"m4a"; suffix=L" 1.5倍速"; options={L"-af",L"atempo=1.5",L"-c:a",L"aac"}; }
        if (!ext.empty()) return ffmpeg(input, suffix, ext, options);
    }
    throw std::runtime_error("action not implemented");
}

} // namespace fw
