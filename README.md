# AST To Wav Converter
 A program that converts a Nintendo AST stream file into a WAV file
 Some games that use this format are The Legend of Zelda: Twilight Princess, Super Mario Galaxy, and Super Mario Galaxy 2

# Usage
 To use the program either drag a file or directory onto the executable or run
 > ast_to_wav.exe InputFile.ast Outputfile.wav loopCount

 Alternatively you can run it over a directory of ast files by running
 > ast_to_wav.exe InputDirectory/ OutputDirectory/ loopCount

# Compiling
 To compile on Windows use the Visual Studio solution
 To Compile on Linux run this on the main.cpp file in the repository
 > g++ main.cpp --std=c++17 -o ast_to_wav -Wno-narrowing