ARCH            = x86_64
TARGET          = loader.efi
OBJS            = main.o

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

main.o: main.c
	gcc $(CFLAGS) -c main.c -o main.o

loader.so: main.o
	ld $(LDFLAGS) main.o -o loader.so -lefi -lgnuefi

loader.efi: loader.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) loader.so loader.efi

clean:
	rm -rf *.o *.so *.efi
