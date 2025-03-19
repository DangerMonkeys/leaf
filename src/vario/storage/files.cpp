#include "storage/files.h"

#include <FS.h>

FileReader::FileReader(fs::FS& fs, String fileName) {
  // open file from SD card
  _file = fs.open(fileName, FILE_READ);
  if (!_file) {
    _error = "Could not open file";
    _complete = true;
  }
  _buffer_count = 0;
  _buffer_index = 0;
}

char FileReader::nextChar() {
  if (_buffer_index >= _buffer_count) {
    // We need to read another block from the file
    _buffer_count = _file.read((uint8_t*)_buffer, FILE_READER_BUFFER_LEN);
    _buffer_index = 0;
  }
  if (_buffer_count == 0) {
    return 0;  // This should not happen, but avoid overrunning the buffer if it does
  }
  return _buffer[_buffer_index++];
}

bool FileReader::contentRemaining() {
  if (_buffer_index < _buffer_count) {
    return true;
  }
  return _file.available();
}

FileReader::~FileReader() { _file.close(); }
