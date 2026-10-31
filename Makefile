# =====================================================================
# UEFI Loader Makefile
# =====================================================================

ARCH            = x86_64
TARGET          = loader.efi

# 定義版本時間戳記（可供程式碼透過 VERSION_STR 讀取）
VERSION         ?= $(shell date +%Y%m%d-%H%M%S)

# gnu-efi 路徑設定（依據您的 Linux 發行版套件路徑調整）
EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIBEFI          = /usr/lib
EFILIB          = /usr/lib

# 連結腳本與目標格式
LDSCRIPT        = $(EFIINC)/gnuefi/elf_$(ARCH)_efi.lds
EFI_CRT_OBJS    = $(EFILIB)/crt0-efi-$(ARCH).o
EFI_LDS         = $(LDSCRIPT)

# =====================================================================
# 核心編譯參數設定（關鍵防禦區段）
# =====================================================================
CFLAGS          = $(EFIINCS) -O2 -fno-stack-protector -fpic -fshort-wchar \
                  -mno-red-zone -mno-sse -mno-avx -mno-mmx -msoft-float \
                  -Wall -Wno-pointer-sign \
                  -DVERSION_STR=\"$(VERSION)\"

LDFLAGS         = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
                  -Bsymbolic -L$(EFILIB) $(EFI_CRT_OBJS)

# 完整編譯流程
all: $(TARGET)

# 將主程式編譯為物件檔
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

# 將物件檔連結為 loader.so（名稱需與 TARGET 呼叫的相符）
loader.so: main.o
	$(LD) $(LDFLAGS) $< -lefi -lgnuefi -o $@

# 將 .so 轉換為標準 UEFI 執行檔 (.efi)
%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $< $@

# 清理編譯產物
clean:
	rm -f *.o *.so *.efi loader.log

.PHONY: all clean
