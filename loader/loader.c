#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_LOADED_IMAGE         *LoadedImage;
  EFI_HANDLE               DriverHandle;
  EFI_HANDLE               TargetHandle;
  EFI_DEVICE_PATH_PROTOCOL *DriverPath;
  EFI_DEVICE_PATH_PROTOCOL *TargetPath;

  // 初始化標準輸出並顯示 Banner
  Print (L"\n========================================\r\n");
  Print (L"   EDK II Loader (Timestamped Build)    \r\n");
  Print (L"========================================\r\n");

  // 1. 取得 LoadedImage Protocol
  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
      Print (L"[Error] Failed to open LoadedImage Protocol: %r\r\n", Status);
      gBS->Stall (2000000);
      return Status;
  }

  // 2. 載入並啟動 UsbXbox360Dxe.efi 驅動程式
  DriverPath = FileDevicePath (LoadedImage->DeviceHandle, L"\\EFI\\drivers\\UsbXbox360Dxe.efi");
  if (DriverPath != NULL) {
      Print (L"[*] Loading driver: \\EFI\\drivers\\UsbXbox360Dxe.efi\r\n");
      Status = gBS->LoadImage (
                      FALSE,
                      ImageHandle,
                      DriverPath,
                      NULL,
                      0,
                      &DriverHandle
                      );
      if (!EFI_ERROR (Status)) {
          Status = gBS->StartImage (DriverHandle, NULL, NULL);
          Print (L"[+] Driver started successfully.\r\n");
      } else {
          Print (L"[-] Notice: Driver load skipped or failed: %r\r\n", Status);
      }
      FreePool (DriverPath);
  }

  // 3. 短暫暫停讓控制器重新掃描綁定
  gBS->Stall (300000);

  // 4. 載入並交棒給目標 Bootloader
  TargetPath = FileDevicePath (LoadedImage->DeviceHandle, L"\\EFI\\BOOT\\bootx64.efi");
  if (TargetPath != NULL) {
      Print (L"[*] Loading target bootloader: \\EFI\\BOOT\\bootx64.efi\r\n");
      Status = gBS->LoadImage (
                      FALSE,
                      ImageHandle,
                      TargetPath,
                      NULL,
                      0,
                      &TargetHandle
                      );
      if (!EFI_ERROR (Status)) {
          Print (L"[+] Handing over control to bootloader now...\r\n");
          gBS->Stall (500000);
          Status = gBS->StartImage (TargetHandle, NULL, NULL);
      } else {
          Print (L"[Error] Failed to load target bootloader: %r\r\n", Status);
      }
      FreePool (TargetPath);
  }

  // 若交棒失敗，停下等待重設
  Print (L"[Error] Boot transition failed. Press any key to reset...\r\n");
  EFI_INPUT_KEY Key;
  UINTN Index;
  gBS->WaitForEvent (1, &SystemTable->ConIn->WaitForKey, &Index);
  SystemTable->ConIn->ReadKeyStroke (SystemTable->ConIn, &Key);

  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
