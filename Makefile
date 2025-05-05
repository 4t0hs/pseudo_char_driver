obj-m := pcd.o
ifdef multiple
obj-m := multiple_pcd.o
endif

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
