obj-m += telegram_fs.o
telegram_fs-objs := tg_mod.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	gcc -Wall -o telegram_server telegram_server.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f telegram_server
