ARCH            = x86_64
OBJS            = main.o
TARGET          = loader.efi

# 支援外部帶入版本字串
VERSION         ?= UNKNOWN

EFIINC          = /usr/include/efi
EFIINCS         = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIB             = /usr/lib
EFILIB          = /usr/lib
EFI_CRT_OBJS    = $(LIB)/crt0-efi-$(ARCH).o
EFI_LDS         = $(LIB)/elf_x86_64_efi.lds

CFLAGS          = $(EFIINCS) -O2 -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall \
                  -DVERSION_STR=\"$(VERSION)\"

ifeq ($(ARCH),x86_64)
CFLAGS          += -DEFI_FUNCTION_WRAPPER
endif

LDFLAGS         = -nostdlib -znocombreloc -T $(EFI_LDS) -shared -Bsymbolic $(EFI_CRT_OBJS) -L $(EFILIB) -lefi -lgnuefi

all: $(TARGET)

%.so: %.o
	ld $(LDFLAGS) $^ -o $@ -lefi -lgnuefi

%.efi: %.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $< $@

main.o: main.c
	gcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o *.so *.efi
