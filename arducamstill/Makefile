CROSS_COMPILE ?=
CC	:= $(CROSS_COMPILE)gcc
CFLAGS ?= -Iinclude -Ivcos -I/opt/vc/include -pipe -W -Wall -Wextra -g -O2
LDFLAGS	?=
LIBS	:= -L/opt/vc/lib -lrt -lbcm_host -lvcos -lvchiq_arm -pthread -lmmal_core -lmmal_util -lmmal_vc_client -lvcsm

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

all: arducamstill

arducamstill: arducamstill.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	-rm -f *.o
	-rm -f arducamstill

