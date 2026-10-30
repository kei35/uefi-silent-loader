TARGET   = loader.efi
SOURCE   = loader.c
OBJS     = loader.o

CC       = x86_64-w64-mingw32-gcc
CFLAGS   = -I/usr/include/efi -I/usr/include/efi/x86_64 -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall
LDFLAGS  = -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L/usr/lib -lefi -lgnuefi

all: $(TARGET)

$(TARGET): $(OBJS)
    $(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
    $(CC) $(CFLAGS) -c $< -o $@

clean:
    rm -f $(OBJS) $(TARGET)
