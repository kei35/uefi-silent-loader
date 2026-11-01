#include <efi.h>
#include <efilib.h>

#define MAX_PATH_LEN 256
#define BUFFER_SIZE  1024

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
            if (*p == '\r' || *p == '\n' || *p == '#') {
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

BOOLEAN CheckFileExists(EFI_FILE_HANDLE RootDir, CHAR16 *FilePath) {
    EFI_FILE_HANDLE TestFile;
    EFI_STATUS Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &TestFile, FilePath, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(Status) && TestFile != NULL) {
        uefi_call_wrapper(TestFile->Close, 1, TestFile);
        return TRUE;
    }
    return FALSE;
}

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

    // 預設後備路徑
    StrCpy(DriverPath, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
    StrCpy(BootPath, L"\\EFI\\BOOT\\bootx64.efi");

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&LoadedImage);
    if (EFI_ERROR(Status) || LoadedImage == NULL) {
        goto EXECUTE_BOOT;
    }

    EFI_HANDLE DeviceHandle = LoadedImage->DeviceHandle;
    if (DeviceHandle == NULL) {
        DeviceHandle = ImageHandle;
    }

    Status = uefi_call_wrapper(BS->HandleProtocol, 3, DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&FileSystem);
    if (EFI_ERROR(Status) || FileSystem == NULL) {
        goto EXECUTE_BOOT;
    }

    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &RootDir);
    if (EFI_ERROR(Status) || RootDir == NULL) {
        goto EXECUTE_BOOT;
    }

    GetSameDirIniPath(LoadedImage->FilePath, IniPath, MAX_PATH_LEN);

    Status = uefi_call_wrapper(RootDir->Open, 5, RootDir, &IniFile, IniPath, EFI_FILE_MODE_READ, 0);
    
    if (!EFI_ERROR(Status) && IniFile != NULL) {
        CHAR8 Buffer[BUFFER_SIZE] = {0};
        UINTN ReadSize = BUFFER_SIZE - 1;
        Status = uefi_call_wrapper(IniFile->Read, 3, IniFile, &ReadSize, Buffer);

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
        uefi_call_wrapper(IniFile->Close, 1, IniFile);
    }

    // 1. 檢查並載入 DXE 驅動（若檔案存在）
    if (DriverPath[0] != L'\0') {
        if (CheckFileExists(RootDir, DriverPath)) {
            EFI_HANDLE DriverHandle;
            EFI_DEVICE_PATH *DriverDevicePath = FileDevicePath(DeviceHandle, DriverPath);
            if (DriverDevicePath != NULL) {
                Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, DriverDevicePath, NULL, 0, &DriverHandle);
                if (!EFI_ERROR(Status)) {
                    uefi_call_wrapper(BS->StartImage, 3, DriverHandle, NULL, NULL);
                }
            }
        }
    }

EXECUTE_BOOT:
    // 2. 檢查並交棒給 Bootloader（若檔案存在）
    if (DeviceHandle != NULL && BootPath[0] != L'\0') {
        if (CheckFileExists(RootDir, BootPath)) {
            EFI_HANDLE BootHandle;
            EFI_DEVICE_PATH *BootDevicePath = FileDevicePath(DeviceHandle, BootPath);
            if (BootDevicePath != NULL) {
                Status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, BootDevicePath, NULL, 0, &BootHandle);
                if (!EFI_ERROR(Status)) {
                    uefi_call_wrapper(BS->StartImage, 3, BootHandle, NULL, NULL);
                }
            }
        }
    }

    return EFI_SUCCESS;
}