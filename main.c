#include <efi.h>
#include <efilib.h>

#define MAX_PATH_LEN 256
#define BUFFER_SIZE  1024

// 定義版本戳記（由 Makefile 在編譯時透過 -DVERSION 注入時間戳記）
#ifndef VERSION_STR
#define VERSION_STR "UNKNOWN"
#endif

// ==========================================
// 1. 字串轉換輔助函式：將 ASCII 轉為 UEFI 使用的 Unicode (CHAR16)
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
// 2. 設定檔解析函式：逐行解析 KEY=VALUE 格式
// ==========================================
VOID ParseConfigLine(CHAR8 *line, CHAR16 *driver_path, CHAR16 *boot_path) {
    // 略過註解行 (# 或 ;) 與空白行
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
        *equal_sign = '\0'; // 切割成 Key 與 Value
        CHAR8 *key = line;
        CHAR8 *val = equal_sign + 1;

        // 清除結尾的換行字元
        for (CHAR8 *p = val; *p != '\0'; p++) {
            if (*p == '\r' || *p == '\n') {
                *p = '\0';
                break;
            }
        }

        // 判斷並對應填入變數
        if (strcmpa((UINT8*)key, (UINT8*)"DRIVER") == 0) {
            AsciiToUnicode(val, driver_path, MAX_PATH_LEN);
        } else if (strcmpa((UINT8*)key, (UINT8*)"BOOTLOADER") == 0) {
            AsciiToUnicode(val, boot_path, MAX_PATH_LEN);
        }
    }
}

// ==========================================
// 3. 主程式入口 (UEFI 程式從這裡開始執行)
// ==========================================
EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir, IniFile;

    CHAR16 DriverPath[MAX_PATH_LEN] = {0};
    CHAR16 BootPath[MAX_PATH_LEN] = {0};

    // 取得當前開機映像檔與硬碟分割區的檔案系統協定
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status)) return Status;

    // 嘗試開啟同目錄下的 loader.ini 設定檔
    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, L"\\EFI\\BOOT\\loader.ini", EFI_FILE_MODE_READ, 0);
    
    // 如果找不到 loader.ini（代表是第一次執行）
    if (EFI_ERROR(Status)) {
        // ==========================================
        // 【修改點 A】在這裡可以更改「第一次自動產生的預設內容」
        // ==========================================
        CHAR8 *DefaultConfig = 
            "# UEFI Silent Loader Config\n"
            "DRIVER=\\EFI\\drivers\\UsbXbox360Dxe.efi\n"
            "BOOTLOADER=\\EFI\\BOOT\\bootx64.efi\n";
        
        // 自動建立並寫入預設的 loader.ini 檔案
        Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, L"\\EFI\\BOOT\\loader.ini", 
                                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
        if (!EFI_ERROR(Status)) {
            UINTN WriteSize = AsciiStrLen(DefaultConfig);
            uefi_call_wrapper(IniFile->Write, 3, IniFile, &WriteSize, DefaultConfig);
            uefi_call_wrapper(IniFile->Close, 1, IniFile);
        }

        // ==========================================
        // 【修改點 B】在這裡更改程式碼內建的預設驅動與開機器路徑
        // ==========================================
        StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi"); // 預設驅動路徑
        StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");             // 預設交棒引導器路徑
    } else {
        // 如果 loader.ini 存在，則讀取其內容進行解析
        CHAR8 Buffer[BUFFER_SIZE] = {0};
        UINTN ReadSize = BUFFER_SIZE - 1;
        Status = uefi_call_wrapper(IniFile->Read, 3, IniFile, &ReadSize, Buffer);
        uefi_call_wrapper(IniFile->Close, 1, IniFile);

        if (!EFI_ERROR(Status) && ReadSize > 0) {
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
    }

    // 防呆機制：如果路徑為空則賦予後備預設值
    if (DriverPath[0] == L'\0') StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
    if (BootPath[0] == L'\0')   StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");

    // ==========================================
    // 4. 執行動作一：靜默載入並啟動 DXE 驅動（例如手把驅動）
    // ==========================================
    EFI_HANDLE DriverHandle;
    EFI_DEVICE_PATH *DriverDevicePath = FileDevicePath(LoadedImage->DeviceHandle, DriverPath);
    if (DriverDevicePath != NULL) {
        if (uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, DriverDevicePath, NULL, 0, &DriverHandle) == EFI_SUCCESS) {
            uefi_call_wrapper(BS->StartImage, 3, DriverHandle, NULL, NULL);
        }
    }

    // ==========================================
    // 5. 執行動作二：靜默載入並無縫交棒給 GRUB
    // ==========================================
    EFI_HANDLE GrubHandle;
    EFI_DEVICE_PATH *GrubDevicePath = FileDevicePath(LoadedImage->DeviceHandle, BootPath);
    if (GrubDevicePath != NULL) {
        if (uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, GrubDevicePath, NULL, 0, &GrubHandle) == EFI_SUCCESS) {
            uefi_call_wrapper(BS->StartImage, 3, GrubHandle, NULL, NULL);
        }
    }

    return EFI_SUCCESS;
}
