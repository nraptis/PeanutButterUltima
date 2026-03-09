#ifndef PEANUT_BUTTER_ULTIMA_MEMORY_HEAP_BUFFER_HPP_
#define PEANUT_BUTTER_ULTIMA_MEMORY_HEAP_BUFFER_HPP_

#include <cstddef>

namespace peanutbutter {

class HeapBuffer {
 public:
  explicit HeapBuffer(std::size_t pSize)
      : mSize(pSize),
        mBuffer(new unsigned char[pSize] {}) {}

  ~HeapBuffer() {
    delete[] mBuffer;
    mBuffer = nullptr;
    mSize = 0;
  }

  HeapBuffer(const HeapBuffer&) = delete;
  HeapBuffer& operator=(const HeapBuffer&) = delete;

  HeapBuffer(HeapBuffer&& pOther) noexcept
      : mSize(pOther.mSize),
        mBuffer(pOther.mBuffer) {
    pOther.mSize = 0;
    pOther.mBuffer = nullptr;
  }

  HeapBuffer& operator=(HeapBuffer&& pOther) noexcept {
    if (this == &pOther) {
      return *this;
    }

    delete[] mBuffer;
    mSize = pOther.mSize;
    mBuffer = pOther.mBuffer;
    pOther.mSize = 0;
    pOther.mBuffer = nullptr;
    return *this;
  }

  unsigned char* Data() {
    return mBuffer;
  }

  const unsigned char* Data() const {
    return mBuffer;
  }

  std::size_t Size() const {
    return mSize;
  }

 private:
  std::size_t mSize = 0;
  unsigned char* mBuffer = nullptr;
};

}  // namespace peanutbutter

#endif  // PEANUT_BUTTER_ULTIMA_MEMORY_HEAP_BUFFER_HPP_
