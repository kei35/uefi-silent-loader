#include <efi.h>
#include <efilib.h>

#define MAX_PATH_LEN 256
#define BUFFER_SIZE  1024

// ==========================================
// 【Log 功能總開關】
// 設為 1 代表開啟 Log 寫入功能，設為 0 則完全關閉
// ==========================================
#define ENABLE_LOGGING 1

// 定義版本戳記（由 Makefile 在編譯時透過 -DVERSION 注入時間戳記）
#ifndef VERSION_STR
#define VERSION_STR "UNKNOWN"
#endif

// ==========================================
// 1. Log 記錄輔助函式：自動在同目錄寫入 loader.log
// ==========================================
VOID WriteLog(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem, CHAR16 *IniPath, CHAR8 *LogMsg) {
#if ENABLE_LOGGING
    EFI_FILE_HANDLE RootDir, LogFile;
    EFI_STATUS Status;

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status)) return;

    // 透過傳入的 IniPath 替換檔名，取得同目錄下的 loader.log 路徑
    CHAR16 LogPath[MAX_PATH_LEN];
    StrCpy(LogPath, IniPath);
    
    // 將路徑結尾的 "loader.ini" 換成 "loader.log"
    INTN LastSlash = -1;
    UINTN Len = StrLen(LogPath);
    for (UINTN i = 0; i < Len; i++) {
        if (LogPath[i] == L'\\') {
            LastSlash = (INTN)i;
        }
    }
    if (LastSlash >= 0) {
        LogPath[LastSlash + 1] = L'\0';
        StrCat(LogPath, L"loader.log");
    } else {
        StrCpy(LogPath, L"\\loader.log");
    }

    // 以 建立/讀寫/附加 模式開啟 log 檔
    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &LogFile, LogPath, 
                               EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR(Status)) {
        // 將檔案指標移到最末端以實現「附加 (Append)」寫入，不會覆蓋之前的紀錄
        UINT64 FileSize = 0;
        // 簡單透過 SetPosition 移到極大值或由 UEFI 自動定位到尾端
        uefi_call_wrapper(LogFile->SetPosition, 2, LogFile, 0xFFFFFFFFFFFFFFFFULL);
        
        UINTN WriteSize = AsciiStrLen(LogMsg);
        uefi_call_wrapper(LogFile->Write, 3, LogFile, &WriteSize, LogMsg);
        uefi_call_wrapper(LogFile->Close, 1, LogFile);
    }
#endif
}

// ==========================================
// 2. 字串轉換輔助函式：將 ASCII 轉為 UEFI 使用的 Unicode (CHAR16)
// ==========================================
VOID AsciiToUnicode(CHAR8 *src, CHAR16 *dst, UINTN max_len) {
    UINTN i = 0;
    while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n' && i < (max_len - 1)) {
        dst[i] = (CHAR16)src[i];
        i++;
    }
    dst[i] = L'\0';
}

// ==========================================
// 3. 設定檔解析函式：逐行解析 KEY=VALUE 格式
// ==========================================
VOID ParseConfigLine(CHAR8 *line, CHAR16 *driver_path, CHAR16 *boot_path) {
    if (line[0] == '#' || line[0] == ';' || line[0] == '\r' || line[0] == '\n' || line[0] == '\0') {
        return;
    }

    CHAR8 *equal_sign = NULL;
    for (CHAR8 *p = line; *p != '\0'; p++) {
        if (*p == '=') {
            equal_sign = p;
            break;
        }
    }

    if (equal_sign != NULL) {
        *equal_sign = '\0';
        CHAR8 *key = line;
        CHAR8 *val = equal_sign + 1;

        for (CHAR8 *p = val; *p != '\0'; p++) {
            if (*p == '\r' || *p == '\n') {
                *p = '\0';
                break;
            }
        }

        if (strcmpa((UINT8*)key, (UINT8*)"DRIVER") == 0) {
            AsciiToUnicode(val, driver_path, MAX_PATH_LEN);
        } else if (strcmpa((UINT8*)key, (UINT8*)"BOOTLOADER") == 0) {
            AsciiToUnicode(val, boot_path, MAX_PATH_LEN);
        }
    }
}

// ==========================================
// 4. 動態取得同目錄設定檔路徑 (.ini) 輔助函式
// ==========================================
VOID GetSameDirIniPath(EFI_DEVICE_PATH *FilePath, CHAR16 *IniPath, UINTN MaxLen) {
    CHAR16 *PathStr = DevicePathToStr(FilePath);
    if (PathStr == NULL) {
        StrCpy(IniPath, L"\\loader.ini");
        return;
    }

    StrCpy(IniPath, PathStr);

    INTN LastSlash = -1;
    UINTN Len = StrLen(IniPath);
    for (UINTN i = 0; i < Len; i++) {
        if (IniPath[i] == L'\\') {
            LastSlash = (INTN)i;
        }
    }

    if (LastSlash >= 0) {
        IniPath[LastSlash + 1] = L'\0';
        StrCat(IniPath, L"loader.ini");
    } else {
        StrCpy(IniPath, L"\\loader.ini");
    }
}

// ==========================================
// 5. 主程式入口
// ==========================================
EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir, IniFile;

    CHAR16 IniPath[MAX_PATH_LEN] = {0};
    CHAR16 DriverPath[MAX_PATH_LEN] = {0};
    CHAR16 BootPath[MAX_PATH_LEN] = {0};

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status)) return Status;

    GetSameDirIniPath(LoadedImage->FilePath, IniPath, MAX_PATH_LEN);

    // 寫入開機啟動 Log
    WriteLog(FileSystem, IniPath, "[INIT] Silent Loader Started.\n");

    CHAR8 *DefaultConfig = 
        "# UEFI Silent Loader Config\n"
        "DRIVER=\\EFI\\drivers\\UsbXbox360Dxe.efi\n"
        "BOOTLOADER=\\EFI\\BOOT\\bootx64.efi\n";

    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, IniPath, 
                               EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    
    if (EFI_ERROR(Status)) {
        WriteLog(FileSystem, IniPath, "[CONFIG] Failed to open/create loader.ini, using fallback paths.\n");
        StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
        StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");
    } else {
        CHAR8 Buffer[BUFFER_SIZE] = {0};
        UINTN ReadSize = BUFFER_SIZE - 1;
        Status = uefi_call_wrapper(IniFile->Read, 3, IniFile, &ReadSize, Buffer);

        if (EFI_ERROR(Status) || ReadSize == 0) {
            WriteLog(FileSystem, IniPath, "[CONFIG] loader.ini is empty, writing default config.\n");
            UINTN WriteSize = AsciiStrLen(DefaultConfig);
            uefi_call_wrapper(IniFile->SetPosition, 2, IniFile, 0);
            uefi_call_wrapper(IniFile->Write, 3, IniFile, &WriteSize, DefaultConfig);
            
            StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
            StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");
        } else {
            WriteLog(FileSystem, IniPath, "[CONFIG] loader.ini successfully loaded and parsed.\n");
            Buffer[ReadSize] = '\0';
            CHAR8 *line = Buffer;
            CHAR8 *next_line = NULL;

            while (line && *line != '\0') {
                next_line = NULL;
                for (CHAR8 *p = line; *p != '\0'; p++) {
                    if (*p == '\n') {
                        *p = '\0';
                        next_line = p + 1;
                        break;
                    }
                }
                ParseConfigLine(line, DriverPath, BootPath);
                line = next_line;
            }
        }
        uefi_call_wrapper(IniFile->Close, 1, IniFile);
    }

    if (DriverPath[0] == L'\0') StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
    if (BootPath[0] == L'\0')   StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");

    // ==========================================
    // 6. 載入並啟動 DXE 驅動
    // ==========================================
    WriteLog(FileSystem, IniPath, "[DRIVER] Loading DXE driver...\n");
    EFI_HANDLE DriverHandle;
    EFI_DEVICE_PATH *DriverDevicePath = FileDevicePath(LoadedImage->DeviceHandle, DriverPath);
    if (DriverDevicePath != NULL) {
        Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, DriverDevicePath, NULL, 0, &DriverHandle);
        if (!EFI_ERROR(Status)) {
            Status = uefi_call_wrapper(BS->StartImage, 3, DriverHandle, NULL, NULL);
            if (!EFI_ERROR(Status)) {
                WriteLog(FileSystem, IniPath, "[DRIVER] DXE driver started successfully.\n");
            } else {
                WriteLog(FileSystem, IniPath, "[DRIVER] Warning: StartImage failed for DXE driver.\n");
            }
        } else {
            WriteLog(FileSystem, IniPath, "[DRIVER] Warning: LoadImage failed for DXE driver (Check path).\n");
        }
    }

    // ==========================================
    // 7. 載入並交棒給下一個開機引導器
    // ==========================================
    WriteLog(FileSystem, IniPath, "[BOOTLOADER] Handing over to next bootloader...\n");
    EFI_HANDLE BootHandle;
    EFI_DEVICE_PATH *BootDevicePath = FileDevicePath(LoadedImage->DeviceHandle, BootPath);
    if (BootDevicePath != NULL) {
        Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, BootDevicePath, NULL, 0, &BootHandle);
        if (!EFI_ERROR(Status)) {
            uefi_call_wrapper(BS->StartImage, 3, BootHandle, NULL, NULL);
        } else {
            WriteLog(FileSystem, IniPath, "[BOOTLOADER] Error: Failed to load target bootloader!\n");
        }
    }

    return EFI_SUCCESS;
}
