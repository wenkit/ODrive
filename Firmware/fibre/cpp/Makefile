
# pkgconf --cflags libusb
# pkgconf --libs libusb

# Prerequisites:
#  Ubuntu: libusb-dev

FILES=libfibre.cpp \
	platform_support/libusb_transport.cpp \
	legacy_protocol.cpp \
	legacy_object_client.cpp \
	logging.cpp

linux-local:
	g++ -shared -o libfibre.so -fPIC -std=c++17 -I/usr/include/libusb-1.0 -I./include -DFIBRE_COMPILE -DFIBRE_ENABLE_CLIENT \
		$(FILES) \
		-lusb-1.0

linux-cross:
	arm-linux-gnueabihf-g++ -march=armv7 -shared -o libfibre.so -fPIC -std=c++17 -I/usr/include/libusb-1.0 -I./include -DFIBRE_COMPILE -DFIBRE_ENABLE_CLIENT \
		$(FILES) \
		-lusb

windows:
	g++ -shared -o libfibre.dll -fPIC -std=c++17 -I./third_party/libusb-windows/libusb-1.0.23/include/libusb-1.0 -I./include -DFIBRE_COMPILE -DFIBRE_ENABLE_CLIENT \
		$(FILES) \
		-static-libgcc -Wl,-Bstatic -lstdc++ ./third_party/libusb-windows/libusb-1.0.23/MinGW64/static/libusb-1.0.a -Wl,-Bdynamic

# nm -D libfibre.so | grep T
