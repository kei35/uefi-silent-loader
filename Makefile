# =====================================================================
# UEFI Loader Makefile (支援同時產出 Debug 與 Quiet 雙版本)
# =====================================================================

ARCH            = x86_64

# 定義版本時間戳記（可供程式碼透過 VERSION_STR 讀取）
VERSION         ?= $(shell date +%Y%m%d-%H%M%S)

# gnu-efi 標頭檔與函式庫路徑設定
EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
EFILIB          = /usr/lib

# ---------------------------------------------------------------------
# 自動尋找連結腳本與 crt0 物件
# ---------------------------------------------------------------------
LDSCRIPT        ?= $(shell find /usr/lib* /usr/include -name "elf_$(ARCH)*.lds" 2>/dev/null | head -n 1)
EFI_CRT_OBJS    ?= $(shell find /usr/lib* -name "crt0-efi-$(ARCH).o" 2>/dev/null | head -n 1)

ifeq ($(LDSCRIPT),)
    LDSCRIPT     = /usr/lib/gnu-efi/elf_$(ARCH).lds
endif
ifeq ($(EFI_CRT_OBJS),)
    EFI_CRT_OBJS = /usr/lib/crt0-efi-$(ARCH).o
endif

EFI_LDS         = $(LDSCRIPT)

# 核心編譯參數設定
CFLAGS          = $(EFIINCS) -O2 -fno-stack-protector -fpic -fshort-wchar \
                  -mno-red-zone -mno-sse -mno-avx -mno-mmx -msoft-float \
                  -Wall -Wno-pointer-sign \
                  -DVERSION_STR=\"$(VERSION)\"

LDFLAGS         = -nostdlib -znocombreloc -T $(EFI_LDS) -shared \
                  -Bsymbolic -L$(EFILIB) $(EFI_CRT_OBJS)

# 預設執行 make 時，同時產出這兩個版本
all: loader-debug.efi loader-quiet.efi

# =====================================================================
# 1. Debug 版本 (由 main.c 編譯)
# =====================================================================
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

loader-debug.so: main.o
	$(LD) $(LDFLAGS) $< -lefi -lgnuefi -o $@

loader-debug.efi: loader-debug.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $< $@

# =====================================================================
# 2. Quiet 版本 (由 main-QUIET.c 編譯)
# =====================================================================
main-QUIET.o: main-QUIET.c
	$(CC) $(CFLAGS) -c $< -o $@

loader-quiet.so: main-QUIET.o
	$(LD) $(LDFLAGS) $< -lefi -lgnuefi -o $@

loader-quiet.efi: loader-quiet.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $< $@

# 清理編譯產物
clean:
	rm -f *.o *.so *.efi loader.log

.PHONY: all clean
