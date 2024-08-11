all: build/Makefile
	$(MAKE) -C build --no-print-directory
fuzz: fuzz/Makefile
	$(MAKE) -C fuzz --no-print-directory
	./fuzz/certifaiger/certifuzzer ./fuzz/voiraig 2
debug: debug/Makefile
	$(MAKE) -C debug --no-print-directory
	debug/certifaiger/certified 'valgrind ./debug/voiraig --verbosity=5' fuzz/certifaiger/bug.aag fuzz/certifaiger/wit.aag
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
fuzz/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Fuzzing -B fuzz
debug/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B debug
clean:
	rm -rf build fuzz debug
.PHONY: all fuzz clean debug
