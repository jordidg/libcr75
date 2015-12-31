
include Makefile.inc

DRIVER_DIR=${DESTDIR}/${USBDROPDIR}/libcr75.bundle

CC=${BUILD}-gcc

SOURCES=ifdhandler.c

all:	libcr75.so

libcr75.so: ${SOURCES}
	${CC} -o libcr75.so ${SOURCES} -fPIC -D_REENTRANT -DIFDHANDLERv2 -Wall -I. ${CFLAGS} ${LDFLAGS} -lusb-1.0 -shared

clean-all:	clean
	rm Makefile.inc || true

clean:
	rm -f *~ *.o *.so || true

install:	all
	install -c -d "${DRIVER_DIR}/Contents"
	install -c -m 0644 Info.plist "${DRIVER_DIR}/Contents"
	install -c -d "${DRIVER_DIR}/Contents/Linux"
	install -c -m 0755 libcr75.so "${DRIVER_DIR}/Contents/Linux"
