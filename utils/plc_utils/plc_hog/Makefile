SRCS=act_plc_hog.c
CFLAGS=-I${SRCROOT}/act_drivers/plc_ldisc
EXEC=act_plc_hog
SCRIPT=plc_hog_startstop.sh

default: all

all:${SRCS}
	${CC} ${CFLAGS} ${SRCS} -o ${EXEC}

install:${EXEC}
	${RUN_AS_ROOT} ${COPY} ${EXEC} ${INSTALL_PREFIX}/bin/
	${RUN_AS_ROOT} ${COPY} ${SCRIPT} ${INSTALL_PREFIX}/bin/

clean:
	rm -f ${EXEC}

uninstall:
	-${RUN_AS_ROOT} ${DELETE} ${INSTALL_PREFIX}/bin/${EXEC}
 
