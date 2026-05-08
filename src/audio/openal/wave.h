// From https://gist.github.com/SteelPh0enix/e44d4a030dd8816309af84809ed75604

#ifndef WAVE_H
#define WAVE_H
#include <stddef.h>
#include <stdint.h>

// Normalized WAV file header structure
typedef struct WAVHeader_t {
  // Should contain the letters "RIFF"
  char file_id[5];

  // The size of entire file in bytes, minus 8 bytes for chunk_id and
  // chunk_size.
  uint32_t file_size;

  // Should contain the letters "WAVE"
  char format[5];

  // Should contain the letters "fmt "
  char subchunk_id[5];

  // 16 for PCM. This is the size of the rest of the sunchunk which follows this
  // number.
  uint32_t subchunk_size;

  // PCM = 1, values other than 1 indicate some form of compression
  uint16_t audio_format;

  // mono = 1, stereo = 2, etc.
  uint16_t number_of_channels;

  // self-explanatory
  uint32_t sample_rate;

  // sample_rate * number of channels * bits per sample / 8
  uint32_t byte_rate;

  // number of channels * bits per sample / 8. Number of bytes for one sample
  // including all channels.
  uint16_t block_align;

  // self-explanatory. BITS, not BYTES.
  uint16_t bits_per_sample;

  // Should contain the letters "data"
  char data_id[5];

  // number of samples * number of channels * bits per sample / 8
  // Actual number of bytes in the sound data.
  uint32_t data_size;
} WAVHeader;

// WAV file with header and pointer to data
typedef struct WAVFile_t {
  // WAV header (copy re-constructed from the file with proper byte aligment)
  WAVHeader header;

  // Pointer to audio data
  uint8_t* data;

  // Length of data, copy of header.data_size field
  uint32_t data_length;
} WAVFile;

// Parse the header of WAV file and return WAVFile structure with header and
// pointer to data
WAVFile WAV_ParseFileData(uint8_t const* data);

#endif