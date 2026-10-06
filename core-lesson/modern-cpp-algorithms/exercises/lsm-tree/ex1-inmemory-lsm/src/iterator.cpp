#include "lsm/iterator.hpp"
#include "lsm/todo.hpp"

namespace lsm {

std::unique_ptr<Iterator> NewEmptyIterator(Status) { Todo("NewEmptyIterator"); }
std::unique_ptr<Iterator> NewVectorIterator(std::vector<std::pair<std::string, std::string>>, CompareFn) {
    Todo("NewVectorIterator");
}
std::unique_ptr<Iterator> NewMergingIterator(CompareFn, std::vector<std::unique_ptr<Iterator>>, bool) {
    Todo("NewMergingIterator");
}
std::unique_ptr<Iterator> NewDBIterator(std::unique_ptr<Iterator>, SequenceNumber) { Todo("NewDBIterator"); }

}  // namespace lsm
