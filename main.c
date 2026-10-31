#include <efi.h>
#include <efilib.h>

#define MAX_PATH_LEN 256
#define BUFFER_SIZE  1024

// 自訂安全字串長度計算
UINTN SafeAsciiStrLen(CONST CHAR8 *Str) {
    UINTN Len = 0;
    while (Str && Str[Len] != '\0') {
        Len++;
    }
    return Len;
}

VOID AsciiToUnicode(CHAR8 *src, CHAR16 *dst, UINTN max_len) {
    UINTN i = 0;
    while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n' && i < (max_len - 1)) {
        dst[i] = (CHAR16)src[i];
        i++;
    }
    dst[i] = L'\0';
}

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

VOID GetSameDirIniPath(EFI_DEVICE_PATH *FilePath, CHAR16 *IniPath, UINTN MaxLen) {
    if (FilePath == NULL) {
        StrCpy(IniPath, L"\\loader.ini");
        return;
    }

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

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut);
    Print(L"[Loader] Starting up (Safe Read-Only Mode)...\n");

    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_HANDLE RootDir, IniFile;

    CHAR16 IniPath[MAX_PATH_LEN] = {0};
    CHAR16 DriverPath[MAX_PATH_LEN] = {0};
    CHAR16 BootPath[MAX_PATH_LEN] = {0};

    // 預設後備路徑
    StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
    StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL) {
        Print(L"[Error] Failed to get LoadedImage protocol!\n");
        goto 暫停等待按鍵;
    }

    EFI_HANDLE DeviceHandle = LoadedImage->DeviceHandle;
    if (DeviceHandle == NULL) {
        DeviceHandle = ImageHandle;
    }

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status) || FileSystem == NULL) {
        Print(L"[Error] Failed to get FileSystem protocol!\n");
        goto 暫停等待按鍵;
    }

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status) || RootDir == NULL) {
        Print(L"[Error] Failed to open Volume root dir!\n");
        goto 暫停等待按鍵;
    }

    GetSameDirIniPath(LoadedImage->FilePath, IniPath, MAX_PATH_LEN);
    Print(L"[Loader] Opening loader.ini (Read-Only)...\n");

    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, IniPath, EFI_FILE_MODE_READ, 0);
    
    if (EFI_ERROR(Status) || IniFile == NULL) {
        Print(L"[Info] loader.ini not found. Using default paths safely.\n");
    } else {
        CHAR8 Buffer[BUFFER_SIZE] = {0};
        UINTN ReadSize = BUFFER_SIZE - 1;
        Status = uefi_call_wrapper(IniFile->Read, 3, IniFile, &ReadSize, Buffer);

        if (!EFI_ERROR(Status) && ReadSize > 0) {
            Print(L"[Loader] Parsing loader.ini content...\n");
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

    Print(L"[Loader] Loading DXE Driver: %s\n", DriverPath);
    // 1. 載入並啟動 DXE 驅動
    if (DeviceHandle != NULL && DriverPath[0] != L'\0') {
        EFI_HANDLE DriverHandle;
        EFI_DEVICE_PATH *DriverDevicePath = FileDevicePath(DeviceHandle, DriverPath);
        if (DriverDevicePath != NULL) {
            Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, DriverDevicePath, NULL, 0, &DriverHandle);
            if (!EFI_ERROR(Status)) {
                uefi_call_wrapper(BS->StartImage, 3, DriverHandle, NULL, NULL);
            }
        }
    }

    Print(L"[Loader] Handing over to Bootloader: %s\n", BootPath);

暫停等待按鍵:
    // =========================================================================
    // 【按任意鍵繼續】機制
    // =========================================================================
    Print(L"\n--------------------------------------------------\n");
    Print(L"[PAUSE] Logs displayed above. Press ANY KEY to continue...\n");
    Print(L"--------------------------------------------------\n");

    EFI_INPUT_KEY Key;
    
    // 1. 先清空鍵盤緩衝區中殘餘的按鍵
    while (uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key) == EFI_SUCCESS) {
        // 持續讀取直到清空
    }

    // 2. 透過事件等待機制，卡住 CPU 直到偵測到鍵盤輸入
    UINTN EventIndex;
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &EventIndex);

    // 3. 讀取並消耗掉剛剛按下的那一顆按鍵
    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

    // =========================================================================
    // 2. 載入並交棒給下一個開機引導器
    // =========================================================================
    if (DeviceHandle != NULL && BootPath[0] != L'\0') {
        EFI_HANDLE BootHandle;
        EFI_DEVICE_PATH *BootDevicePath = FileDevicePath(DeviceHandle, BootPath);
        if (BootDevicePath != NULL) {
            Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, BootDevicePath, NULL, 0, &BootHandle);
            if (!EFI_ERROR(Status)) {
                uefi_call_wrapper(BS->StartImage, 3, BootHandle, NULL, NULL);
            } else {
                Print(L"[Error] LoadImage failed for bootloader! Status: %r\n", Status);
            }
        }
    }

    return EFI_SUCCESS;
}
