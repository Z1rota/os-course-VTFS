obj-m := vtfs.o
vtfs-y := source/vtfs_main.o source/super.o source/inode.o \
          source/dir.o source/http.o source/file.o

KDIR := /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean