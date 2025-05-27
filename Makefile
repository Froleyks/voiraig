MAKEFLAGS += --no-print-directory
all: build/Makefile
	@cmake --build build --parallel
	@cmake --install build --prefix .
fuzz: fuzz/Makefile
	@cmake --build fuzz --parallel
	@cmake --install fuzz --prefix fuzz
	cd fuzz && ./bin/certifuzzer ./voiraig 2
debug: debug/Makefile
	@cmake --build debug --parallel
	./fuzz/bin/certified 'valgrind ./debug/voiraig --verbosity=5' fuzz/bug.aag fuzz/wit.aag
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -B build
fuzz/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Fuzzing -B fuzz -DCHECK=ON
debug/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B debug
docker: clean
	docker build -t voiraig .
	docker run --rm -it voiraig
clean:
	rm -rf build bin fuzz debug
.PHONY: all fuzz clean docker debug
