#ifndef FILES_H
#define FILES_H

#include <FS.h>

#define FILE_READER_BUFFER_LEN (256)

class FileReader {
 public:
  FileReader(fs::FS& fs, String fileName);

  char nextChar();

  bool contentRemaining();

  inline String error() { return _error; }

  ~FileReader();

 private:
  bool _complete;
  String _error;
  File _file;
  char _buffer[FILE_READER_BUFFER_LEN];
  uint16_t _buffer_count;
  uint16_t _buffer_index;
};

#endif
