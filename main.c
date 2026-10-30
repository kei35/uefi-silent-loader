#include <efi.h>
#include <efilib.h>

#define MAX_PATH_LEN 256
#define BUFFER_SIZE  1024

// 轉化 ASCII 字串為 UEFI Unicode (CHAR16)
VOID AsciiToUnicode(CHAR8 *src, CHAR16 *dst, UINTN max_len) {
    UINTN i = 0;
    while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n' && i < (max_len - 1)) {
        dst[i] = (CHAR16)src[i];
        i++;
    }
    dst[i] = L'\0';
}

// 簡單解析 KEY=VALUE 格式
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

    // 1. 取得當前磁碟分割區根目錄
    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status)) return Status;

    // 2. 嘗試讀取 \EFI\BOOT\loader.ini
    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, L"\\EFI\\BOOT\\loader.ini", EFI_FILE_MODE_READ, 0);
    
    if (EFI_ERROR(Status)) {
        // 如果檔案不存在，自動建立並寫入預設內容
        CHAR8 *DefaultConfig = 
            "# UEFI Silent Loader Config\n"
            "DRIVER=\\EFI\\drivers\\UsbXbox360Dxe.efi\n"
            "BOOTLOADER=\\EFI\\BOOT\\grubx64.efi\n";
        
        Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, L"\\EFI\\BOOT\\loader.ini", 
                                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
        if (!EFI_ERROR(Status)) {
            UINTN WriteSize = AsciiStrLen(DefaultConfig);
            uefi_call_wrapper(IniFile->Write, 3, IniFile, &WriteSize, DefaultConfig);
            uefi_call_wrapper(IniFile->Close, 1, IniFile);
        }

        // 同時將預設值賦予變數
        StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
        StrCpy(BootPath, L"\\EFI\\BOOT\\grubx64.efi");
    } else {
        // 檔案存在，進行讀取與解析
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

    // 後備保險防呆
    if (DriverPath[0] == L'\0') StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
    if (BootPath[0] == L'\0')   StrCpy(BootPath, L"\\EFI\\BOOT\\grubx64.efi");

    // 3. 靜默載入與執行 DXE 驅動
    EFI_HANDLE DriverHandle;
    EFI_DEVICE_PATH *DriverDevicePath = FileDevicePath(LoadedImage->DeviceHandle, DriverPath);
    if (DriverDevicePath != NULL) {
        if (uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, DriverDevicePath, NULL, 0, &DriverHandle) == EFI_SUCCESS) {
            uefi_call_wrapper(BS->StartImage, 3, DriverHandle, NULL, NULL);
        }
    }

    // 4. 靜默載入並交棒給 GRUB
    EFI_HANDLE GrubHandle;
    EFI_DEVICE_PATH *GrubDevicePath = FileDevicePath(LoadedImage->DeviceHandle, BootPath);
    if (GrubDevicePath != NULL) {
        if (uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, GrubDevicePath, NULL, 0, &GrubHandle) == EFI_SUCCESS) {
            uefi_call_wrapper(BS->StartImage, 3, GrubHandle, NULL, NULL);
        }
    }

    return EFI_SUCCESS;
}