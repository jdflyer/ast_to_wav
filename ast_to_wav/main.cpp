#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <string>
#include <cstring>
#include <climits>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef int32_t s32;
typedef uint32_t u32;

static const short DSPADPCM_FILTER[16][2] = { //Array found in twilight princess
{0,0},
{0x0800,0},
{0,0x0800},
{0x0400,0x0400},
{0x1000,0xf800},
{0x0e00,0xfa00},
{0x0c00,0xfc00},
{0x1200,0xf600},
{0x1068,0xf738},
{0x12c0,0xf704},
{0x1400,0xf400},
{0x0800,0xf800},
{0x0400,0xfc00},
{0xfc00,0x0400},
{0xfc00,0},
{0xf800,0}
};

template <typename T>
T swap_endian(T u) //taken from https://stackoverflow.com/a/4956493
{
	static_assert (CHAR_BIT == 8, "CHAR_BIT != 8");

	union
	{
		T u;
		unsigned char u8[sizeof(T)];
	} source, dest;

	source.u = u;

	for (size_t k = 0; k < sizeof(T); k++)
		dest.u8[k] = source.u8[sizeof(T) - k - 1];

	return dest.u;
}

struct WAVHeader {
	char ChunkID[4] = { 'R','I','F','F' };
	u32 ChunkSize;
	char Format[4] = { 'W','A','V','E' };
	char subChunk1ID[4] = { 'f','m','t',' ' };
	u32 Subchunk1Size = 16; //16 For PCM
	u16 AudioFormat = 1; //1 For PCM
	u16 NumChannels = 2;
	u32 SampleRate;
	u32 ByteRate; //SampleRate * NumChannels * BitsPerSample/8
	u16 BlockAlign; //NumChannels * BitsPerSample/8
	u16 BitsPerSample = 16; //8 bits = 8, 16 bits = 16, etc.
	char SubChunk2ID[4] = { 'd','a','t','a' };
	u32 Subchunk2Size; //NumSamples * NumChannels * BitsPerSample/8
};

struct AST_Heading {
	char Name[4];
	u32 Size;
	u16 Format; //0=ADPCM	1=PCM16
	u16 BitDepth;
	u16 NumChannels;
	u16 unk_1;
	u32 SampleRate;
	u32 TotalSamples;
	u32 LoopStart; //In Samples
	u32 LoopEnd; //In Samples
	u32 FirstBlockSize;
	u32 unk_2;
	u32 unk_3;
	u8 padding[20];
	void convertToLittleEndian() {
		Size = swap_endian(Size);
		Format = swap_endian(Format);
		BitDepth = swap_endian(BitDepth);
		NumChannels = swap_endian(NumChannels);
		unk_1 = swap_endian(unk_1);
		SampleRate = swap_endian(SampleRate);
		TotalSamples = swap_endian(TotalSamples);
		LoopStart = swap_endian(LoopStart);
		LoopEnd = swap_endian(LoopEnd);
		FirstBlockSize = swap_endian(FirstBlockSize);
		unk_2 = swap_endian(unk_2);
		unk_3 = swap_endian(unk_3);
	}
};

struct BLCK_Header {
	char Blck_Name[4];
	u32 Blck_Size;
	u8 padding[24];
};

struct Block_Data {
	std::vector<s16> data;
};

struct BLCK {
	BLCK_Header header;
	std::vector<Block_Data> BlockData;
};

enum AST_FORMAT {
	AST_FORMAT_ADPCM = 0,
	AST_FORMAT_PCM16 = 1
};

int convertAST(char* filename, char* outputname, u32 loopcount) {
	std::cout << "Converting " << filename << " to " << outputname << '\n';
	std::ifstream file(filename, std::ios::binary | std::ios::in);
	if (file.is_open() == false) {
		std::cout << "Error: Couldn't open file " << filename << "!\n";
		return -1;
	}
	char headerBuffer[sizeof(AST_Heading)];
	file.read(headerBuffer, sizeof(AST_Heading));
	AST_Heading* heading = reinterpret_cast<AST_Heading*>(headerBuffer);
	heading->convertToLittleEndian();

	//std::cout << "Loop Start is " << heading->LoopStart << " samples!\n";
	//std::cout << "Loop End is " << heading->LoopEnd << " samples!\n";

	std::vector<Block_Data> data(heading->NumChannels);

	size_t BytesRead = sizeof(AST_Heading);
	bool isReadingFile = true;
	while (isReadingFile) {
		char BLCK_Header_Buffer[sizeof(BLCK_Header)];
		file.read(BLCK_Header_Buffer, sizeof(BLCK_Header));
		BLCK_Header* block_header = reinterpret_cast<BLCK_Header*>(BLCK_Header_Buffer);
		if (strncmp(block_header->Blck_Name, "BLCK", 4) != 0) {
			std::cout << "ERROR: BLCK NOT VALID! NAME IS " << block_header->Blck_Name << " Read Pos is 0x" << std::hex << file.tellg() << '\n';
			return -1;
		}
		u32 Blck_Size = swap_endian(block_header->Blck_Size);
		BytesRead = BytesRead + sizeof(BLCK_Header);
		file.seekg(BytesRead);
		for (int i = 0; i < heading->NumChannels; i++) {
			char* Buffer = new char[Blck_Size];
			file.read(Buffer, Blck_Size);
			if (heading->Format == AST_FORMAT_ADPCM) {
				for (int j = 0; j < Blck_Size; j = j + 9) { //ADPCM data is stored as 2 heading nibbles and 16 data nibbles
					//Code here is based on the in_cube winamp plugin source code for converting adpcm to pcm16
					char* input = (char*)&Buffer[j];
					s16 delta = 1 << (((*input) >> 4) & 0b1111);
					s16 adpcm_id = (*input) & 0b1111;
					s16 nibbles[16];

					input = input + 1;

					for (int k = 0; k < 16; k = k + 2) {
						nibbles[k] = ((*input & 0b11111111) >> 4);
						nibbles[k + 1] = (*input & 0b11111111 & 0b1111);
						input = input + 1;
					}
					for (int k = 0; k < 16; k++) {
						if (nibbles[k] >= 8) { //If signed bit on nibble set to negative
							nibbles[k] = nibbles[k] - 16;
						}
						s16 hist;
						s16 hist2;
						size_t data_size = data[i].data.size();
						if (data_size == 0) {
							hist = 0;
							hist2 = 0;
						}
						else if (data_size == 1) {
							hist = data[i].data[data_size - 1];
							hist2 = 0;
						}
						else {
							hist = data[i].data[data_size - 1];
							hist2 = data[i].data[data_size - 2];
						}
						int sample = (delta * nibbles[k]) << 11;
						sample = sample + ((long)hist * DSPADPCM_FILTER[adpcm_id][0]) + ((long)hist2 * DSPADPCM_FILTER[adpcm_id][1]);
						sample = sample >> 11;

						if (sample > 32767) {
							sample = 32767;
						}
						else if (sample < -32767) {
							sample = -32767;
						}
						data[i].data.push_back((s16)sample);
					}
				}
			}
			else if (heading->Format == AST_FORMAT_PCM16) {
				s16* data_array = reinterpret_cast<s16*>(Buffer);
				for (int j = 0; j < Blck_Size / 2; j++) {
					data[i].data.push_back(swap_endian(data_array[j]));
				}
			}
			delete[] Buffer;
			BytesRead = BytesRead + Blck_Size;
			file.seekg(BytesRead);
		}
		if (BytesRead >= heading->Size + 0x40) {
			isReadingFile = false;
		}
	}
	file.close();

	u8 fadeout_in_seconds;
	if (heading->LoopStart > 0 && loopcount > 0) {
		fadeout_in_seconds = 15;
	}
	else {
		fadeout_in_seconds = 0;
	}
	u32 fadeout_in_samples = fadeout_in_seconds * heading->SampleRate;

	WAVHeader out_header;
	out_header.NumChannels = heading->NumChannels;
	out_header.SampleRate = heading->SampleRate;
	out_header.ByteRate = out_header.SampleRate * out_header.NumChannels * (out_header.BitsPerSample / 8);
	out_header.BlockAlign = out_header.NumChannels * (out_header.BitsPerSample / 8);
	out_header.Subchunk2Size = (heading->LoopEnd * out_header.NumChannels * (out_header.BitsPerSample / 8)) + (loopcount * (heading->LoopEnd - heading->LoopStart) * out_header.NumChannels * (out_header.BitsPerSample / 8)) + (fadeout_in_samples * out_header.NumChannels * (out_header.BitsPerSample / 8));
	out_header.ChunkSize = 36 + out_header.Subchunk2Size;

	std::ofstream out_file(outputname, std::ios::out | std::ios::binary);
	size_t outbuffer_size = sizeof(WAVHeader) + (heading->LoopEnd * sizeof(s16) * heading->NumChannels);
	char* out_buffer = new char[outbuffer_size];
	memcpy(out_buffer, &out_header, sizeof(WAVHeader)); //Copies WAV Header Data to the output buffer (Assumes output header will be larger than WAVHeader length)
	size_t data_size = heading->LoopEnd;
	for (u32 track_index = 0; track_index < heading->NumChannels; track_index++) {
		for (size_t i = 0; i < data_size; i++) {
			s16* placeToWriteTo = (s16*)&out_buffer[sizeof(WAVHeader) + (track_index * sizeof(s16)) + (i * heading->NumChannels * sizeof(s16))]; //Gets the right address to put the sound data into
			*placeToWriteTo = data[track_index].data[i];
		}
	}

	out_file.write(out_buffer, outbuffer_size);

	if (heading->LoopStart > 0 && loopcount > 0) {
		char* loopStartPos = &out_buffer[sizeof(WAVHeader) + (heading->LoopStart * sizeof(s16) * heading->NumChannels)];
		size_t loopEnd_Size = (heading->LoopEnd - heading->LoopStart) * heading->NumChannels * sizeof(s16);
		for (int i = 0; i < loopcount; i++) {
			out_file.write(loopStartPos, loopEnd_Size);
		}
		if (fadeout_in_samples <= (heading->LoopEnd - heading->LoopStart)) {
			size_t fadeout_size = (fadeout_in_samples * heading->NumChannels * sizeof(s16));
			char* fadeout_buffer = new char[fadeout_size];
			memcpy(fadeout_buffer, loopStartPos, fadeout_size);
			for (int i = 0; i < fadeout_in_samples; i++) {
				for (int track_index = 0; track_index < heading->NumChannels; track_index++) {
					s16* locToWriteTo = (s16*)&fadeout_buffer[(i * heading->NumChannels * sizeof(s16)) + (track_index * sizeof(s16))];
					int inverted = fadeout_in_samples - i;
					float fade_percent = (float)inverted / (float)fadeout_in_samples;
					float fade_float = (*locToWriteTo) * fade_percent;
					*locToWriteTo = (s16)fade_float;
				}
			}
			out_file.write(fadeout_buffer, fadeout_size);
			delete[] fadeout_buffer;
		}
	}

	delete[] out_buffer;

	if (out_file.fail()) {
		std::cout << "ERROR WHILE WRITING TO FILE!\n";
		out_file.close();
		return -1;
	}
	out_file.close();
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc > 1) {
		char* filename = argv[1];
		u32 loopcount = 1;
		if (argc > 3) {
			std::string loopCountString = std::string(argv[3]);
			loopcount = std::stoi(loopCountString, nullptr, 0);
		}
		if (std::filesystem::is_directory(filename)) {
			char* outDir = (char*)"Converted";
			std::string input_string(filename); //Only used for appending if 2nd arg isn't provided
			if (argc > 2) {
				outDir = argv[2];
			}
			else {
				if (input_string.back() == '\\' || input_string.back() == '/') {
					input_string.pop_back();
				}
				input_string.append("_converted");
				outDir = (char*)input_string.c_str();
			}
			std::filesystem::create_directory(outDir);
			for (const auto& entry : std::filesystem::directory_iterator(filename)) {
				if (entry.path().extension().string() == ".ast") {
					auto outputpath = entry.path();
					outputpath.replace_extension(".wav");
					std::string outputname = outDir;
					outputname.append("\\");
					outputname.append(outputpath.filename().string());
					if (convertAST((char*)entry.path().string().c_str(), (char*)outputname.c_str(), loopcount) == -1) {
						std::cout << "An error occured while converting " << entry.path().string() << "!\n";
					}
				}
			}
			return 0;
		}
		else {
			char* outputname = (char*)"out.wav";
			bool outputnameProvided = false;
			if (argc > 2) {
				outputnameProvided = true;
				outputname = argv[2];
			}
			std::filesystem::path outputnamePath(outputname);
			std::filesystem::path inputnamePath(filename);
			if (outputnamePath.has_extension() == false) {
				outputnamePath.replace_extension(".wav");
			}

			if (outputnameProvided == false) {
				outputnamePath = std::filesystem::path(inputnamePath);
				outputnamePath.replace_extension(".wav");
			}

			if (strcmp(inputnamePath.extension().string().c_str(), ".ast") != 0) {
				std::cout << "Error: Provided file is not an ast file!\n";
				return -1;
			}
			return convertAST((char*)inputnamePath.string().c_str(), (char*)outputnamePath.string().c_str(), loopcount);
		}
	}
	std::cout << "AST Ripper Usage: input_filename.ast output_filename.wav loopCount\nor input_directory/ output_directory/ loopCount";
	return 0;
}