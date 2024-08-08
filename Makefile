all: build/Makefile
	$(MAKE) -C build --no-print-directory
fuzz: fuzz/Makefile
	$(MAKE) -C fuzz --no-print-directory
	./fuzz/certifaiger/certifuzzer ./fuzz/voiraig
debug: debug/Makefile
	$(MAKE) -C debug --no-print-directory
	valgrind debug/certifaiger/certified debug/voiraig fuzz/certifaiger/bug.aag fuzz/certifaiger/wit.aag
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
fuzz/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Fuzzing -B fuzz
debug/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Debug -B debug
clean:
	rm -rf build fuzz debug
.PHONY: all fuzz clean debug
