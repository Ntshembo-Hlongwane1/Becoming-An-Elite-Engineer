#include "am/aligned_buffer.hpp"
#include "am/todo.hpp"

namespace am {

AlignedBuffer AlignedBuffer::Allocate(std::size_t, std::size_t) { Todo("AlignedBuffer::Allocate"); }

AlignedBuffer::~AlignedBuffer() {
    // TODO(Ex3): release data_ with the function matching how Allocate obtained it.
    // (Empty so the stub never crashes. ASan will report the leak once Allocate works.)
}

AlignedBuffer::AlignedBuffer(AlignedBuffer&& other) noexcept {
    // TODO(Ex3): steal other's state and leave `other` empty.
    (void)other;
}

AlignedBuffer& AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {
    // TODO(Ex3): release ours, steal theirs, leave `other` empty, survive self-move.
    (void)other;
    return *this;
}

}  // namespace am
