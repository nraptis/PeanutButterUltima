.PHONY: configure-debug build-debug clean-debug Build\ PeanutButterUltima\ Debug

configure-debug:
	cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug

build-debug: configure-debug
	cmake --build build-debug --config Debug --target PeanutButterUltima

Build\ PeanutButterUltima\ Debug: build-debug

clean-debug:
	cmake --build build-debug --config Debug --target clean
