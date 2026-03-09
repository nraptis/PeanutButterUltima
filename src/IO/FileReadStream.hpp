#ifndef PEANUT_BUTTER_ULTIMA_IO_FILE_READ_STREAM_HPP_
#define PEANUT_BUTTER_ULTIMA_IO_FILE_READ_STREAM_HPP_

#include <cstddef>

namespace peanutbutter {

class FileReadStream {
 public:
  virtual ~FileReadStream() = default;
  virtual bool IsReady() const = 0;
  virtual std::size_t GetLength() const = 0;
  virtual bool Read(std::size_t pOffset, unsigned char* pDestination, std::size_t pLength) const = 0;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_IO_FILE_READ_STREAM_HPP_
