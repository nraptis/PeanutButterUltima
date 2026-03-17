#ifndef PEANUT_BUTTER_ULTIMA_IO_FILE_WRITE_STREAM_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_FILE_WRITE_STREAM_HPP_

#include <cstddef>
#include <string>

namespace peanutbutter {

class FileWriteStream {
 public:
  virtual ~FileWriteStream() = default;
  virtual bool IsReady() const = 0;
  virtual bool Write(const unsigned char* pData, std::size_t pLength) = 0;
  virtual std::size_t GetBytesWritten() const = 0;
  virtual bool Close() = 0;
  virtual std::string LastErrorMessage() const = 0;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_IO_FILE_WRITE_STREAM_HPP_
