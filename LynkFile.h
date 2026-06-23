#include <LittleFS.h>

//#define FILE_READ   "r" //Open a file for reading,	read from start if exists, otherwise failure to open
//#define FILE_WRITE  "w" //Create a file for writing,	destroy contents if exists, otherwise create new
//#define FILE_APPEND "a" //Append to a file,	write to end  if exists, otherwise create new
//#define FILE_READ_EXTENDED   "r+" Open a file for read/write, read from start	if exists, otherwise error
//#define FILE_WRITE_EXTENDED  "w+" Create a file for read/write,	destroy contents if exists, otherwise create new
//#define FILE_APPEND_EXTENDED "a+" Open a file for read/write	write to end if exists, otherwise create new

enum FileStatus {
  F_READ,     // read data from file
  F_WRITE,    // write data to file
  F_NO_DIFF,  //no diff(data not changed)
  F_ERROR,    //FS value not initialized or nothing to write
};

class LynkFile {
public:
  LynkFile(fs::FS* nfs = nullptr, const char* path = nullptr, uint8_t version = 1, void* data = nullptr, size_t size = 0) {
    _fs = nfs;
    _path = path;
    _version = version;
    _data = data;
    _size = size;
  }

  FileStatus init() {
    if (!_fs || !_data) return F_ERROR;
    if (!_fs->exists(_path)) return _writeData();  //write init version
    File file = _fs->open(_path, "r");
    if (!file) return F_ERROR;
    size_t size = file.size();
    uint8_t version = file.read();  //first byte of file
    if (version != _version || size != _size + 1) {
      file.close();
      return _writeData();
    }
    file.read((uint8_t*)_data, _size);
    return F_READ;
  }

  FileStatus commit() {
    if (!_fs || !_data) return F_ERROR;
    if (!_fs->exists(_path)) return F_ERROR;
    File file = _fs->open(_path, "r");
    if (!file || file.size() <= 1) return F_ERROR;
    file.read();  // skip version
    if (file.size() != _size + 1) return F_ERROR;
    for (size_t i = 0; i < _size; i++) {
      if (((uint8_t*)_data)[i] != file.read()) {
        file.close();
        return _writeData();
      }
    }
    return F_NO_DIFF;
  }

  uint8_t getVersion() {
    return _version;
  }

private:
  fs::FS* _fs;
  const char* _path;
  uint8_t _version;
  void* _data;
  size_t _size;

  FileStatus _writeData() {
    File file = _fs->open(_path, "w");
    if (!file) return F_ERROR;
    file.write(_version);
    file.write((uint8_t*)_data, _size);
    file.close();
    return F_WRITE;
  }
};