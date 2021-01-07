#!/bin/bash
echo -e "\033[0;32mHow many CPU cores do you want to be used in compiling process? (Default is 1. Press enter for default.)\033[0m"
read -e CPU_CORES
if [ -z "$CPU_CORES" ]
then
	CPU_CORES=1
fi

# Clone SAPP code from SAPP official Github repository
	git clone https://github.com/sappcoin-com/SAPP_1_4

# Entering SAPP directory
	cd SAPP

# Compile dependencies
	cd depends
	make -j$(echo $CPU_CORES) HOST=x86_64-apple-darwin17 
	cd ..

# Compile SAPP
	./autogen.sh
	./configure --prefix=$(pwd)/depends/x86_64-apple-darwin17 --enable-cxx --enable-static --disable-shared --disable-debug --disable-tests --disable-bench
	make -j$(echo $CPU_CORES) HOST=x86_64-apple-darwin17
	make deploy
	cd ..

# Create zip file of binaries
	cp SAPP/src/sapphired SAPP/src/sapphire-cli SAPP/src/sapphire-tx SAPP/src/qt/sapphire-qt SAPP/SAPP.dmg .
	zip SAPP-MacOS.zip sapphired sapphire-cli sapphire-tx sapphire-qt SAPP.dmg
	rm -f sapphired sapphire-cli sapphire-tx sapphire-qt SAPP.dmg
