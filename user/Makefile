INCLUDES	:=	-I./include/

BIN_TOC		:= ../utility/BinaryToC.c

USER_LIB	:= LibMain.o Printf.o uLib.o userHeap.o

MOUNT_DIR	:= ./mnt

USER_TARGET	:= ProcessA.x ProcessB.x ForkTest.x ProcessIdTest.x SysfileTest.x PipeTest.x ExecTest.x ExecToLs.x SyscallTest.x WaitTest.x MkdirTest.x MountTest.x LinkTest.x ls.x sh.x echo.x xargs.x cat.x mkdir.x touch.x rm.x\
		ls sh echo xargs cat mkdir touch rm

.PHONY: bintoc build clean

bintoc:
	gcc $(BIN_TOC) -o BinaryToC
	chmod +x BinaryToC

build: bintoc $(USER_LIB) $(USER_TARGET)

%.x: %.b.c
	$(GCC) $(CFLAGS) -c -o $@ $<

%.b.c: %.b
	./BinaryToC -f $< -o $@

%: Entry.o %.o $(USER_LIB)
	$(LD) -o $@ $(LDFLAGS) -G 0 -static -n -nostdlib -T ../linkscript/User.ld $^
	$(OBJDUMP) -D $@ > $@.txt
	mv $@ $(MOUNT_DIR)

%.b: Entry.o %.o $(USER_LIB)
	$(LD) -o $@ $(LDFLAGS) -G 0 -static -n -nostdlib -T ../linkscript/User.ld $^
	$(OBJDUMP) -D $@ > $@.txt

%.o: %.c
	$(GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.S
	$(GCC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	for d in $(modules); \
		do \
			$(MAKE) --directory=$$d clean; \
		done
	rm -rf *.o *~ *.d *.x *.txt $(MOUNT_DIR)/* BinaryToC *.b

include ../include.mk
