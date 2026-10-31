# =====================================================================
# UEFI Loader Makefile (適用於標準 Linux 環境)
# =====================================================================

ARCH            = x86_64
TARGET          = loader.efi

# 定義版本時間戳記（可供程式碼透過 VERSION_STR 讀取）
VERSION         ?= $(shell date +%Y%m%d-%H%M%S)

# gnu-efi 標頭檔與函式庫路徑設定
EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
EFILIB          = /usr/lib

# ---------------------------------------------------------------------
# 自動尋找連結腳本 (elf_x86_64.lds / elf_x86_64_efi.lds) 與 crt0 物件
# 支援 Ubuntu/Debian, Arch/Manjaro, Fedora 等多種發行版路徑
# ---------------------------------------------------------------------
LDSCRIPT        ?= $(shell find /usr/lib* /usr/include -name "elf_$(ARCH)*.lds" 2>/dev/null | head -n 1)
EFI_CRT_OBJS    ?= $(shell find /usr/lib* -name "crt0-efi-$(ARCH).o" 2>/dev/null | head -n 1)

# 如果自動尋找失敗，回退至 Ubuntu/Debian 預設標準路徑
ifeq ($(LDSCRIPT),)
    LDSCRIPT     = /usr/lib/gnu-efi/elf_$(ARCH).lds
endif
ifeq ($(EFI_CRT_OBJS),)
    EFI_CRT_OBJS = /usr/lib/crt0-efi-$(ARCH).o
endif

EFI_LDS         = $(LDSCRIPT)

# =====================================================================
# 核心編譯參數設定（關鍵防禦區段：防範 #UD 與向量指令崩潰）
# =====================================================================
CFLAGS          = $(EFIINCS) -O2 -fno-stack-protector -fpic -fshort-wchar \
                  -mno-red-zone -mno-sse -mno-avx -mno-mmx -msoft-float \
                  -Wall -Wno-pointer-sign \
                  -DVERSION_STR=\"$(VERSION)\"

LDFLAGS         = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
                  -Bsymbolic -L$(EFILIB) $(EFI_CRT_OBJS)

# 完整編譯流程
all: $(TARGET)

# 1. 將 main.c 編譯為物件檔 main.o
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

# 2. 將 main.o 連結為 loader.so（必須對應 TARGET 產出的 loader.efi）
loader.so: main.o
	$(LD) $(LDFLAGS) $< -lefi -lgnuefi -o $@

# 3. 將 .so 轉換為標準 UEFI 執行檔 (.efi)
%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $< $@

# 清理編譯產物
clean:
	rm -f *.o *.so *.efi loader.log

.PHONY: all clean
