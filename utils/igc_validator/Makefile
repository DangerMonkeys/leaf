CC = cc
CFLAGS = -std=c99 -Os -Wall -Wextra -Wno-deprecated-declarations
LDLIBS = $(if $(filter Darwin,$(shell uname -s)),,-lcrypto)
.PHONY: all windows clean
all: dist/vali-xlv
dist:
	mkdir -p $@
dist/vali-xlv: validate.c | dist
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)
	strip $@
windows: dist/vali-xlv.exe
dist/vali-xlv.exe: validate.c | dist
	i686-w64-mingw32-gcc $(CFLAGS) -static -s $< -o $@ -ladvapi32
clean:
	rm -f dist/vali-xlv dist/vali-xlv.exe
