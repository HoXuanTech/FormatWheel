# Windows 10 版開發狀態

這裡是繁體中文 Windows 10 以上版本的開發程式碼，**尚未達到原版 73 項操作的功能對等，也未通過 Windows 10 實機驗證**。請勿將編譯出的程式當成正式版本發行。

目前使用原生 Win32/C++，已建立繁體中文輪盤、檔案選取、OLE 拖放與 Shift 叫出輪盤的程式架構。拖住檔案按 Shift 的跨程式互動仍需實機驗收。部分圖片轉換使用 Windows Imaging Component；音訊與影片操作依賴外部 `ffmpeg.exe`，PDF 操作依賴 Poppler 工具。這些外部工具目前**沒有隨程式封裝**。

尚缺的原版功能包括圖片去背、OCR、部分 PDF 與文件轉換、文件隱私清理，以及相應的完整測試。Windows 介面的操作清單目前只是對等移植的待辦表，顯示某個格子不表示它已可用。

在 Windows 10/11 開發環境以 Visual Studio 2022 與 CMake 建置：

```powershell
cmake -S windows -B windows/build -G "Visual Studio 17 2022" -A x64
cmake --build windows/build --config Release
```

目標平台是 Windows 10 以上；Windows 8/8.1 已不在支援範圍。正式發行前仍需逐項核對 73 項操作、批次處理、隱私掃描與清除，以及拖曳加 Shift 的輪盤流程。
