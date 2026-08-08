#pragma once
#include <vector>
#include "BPlusTreeNode.hpp"
#include "InternalNode.hpp"
#include "LeafNode.hpp"
#include "RecordID.hpp"
#include <stdexcept>
#include <iostream>
#include <string>
#include <cstddef>

template<typename KeyType>
class BPlusTree
{
public:

    explicit BPlusTree(std::size_t order) : m_Order(order) {
        if (order < 3) {
            throw std::invalid_argument("BPlusTree order must be at least 3.");
        }
    }

    ~BPlusTree() {
        Clear();
    }

    // The tree owns raw node pointers, so copying would double-free.
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    BPlusTree(BPlusTree&& other) noexcept
        : m_Order(other.m_Order), m_Size(other.m_Size), m_Root(other.m_Root) {
        other.m_Size = 0;
        other.m_Root = nullptr;
    }

    BPlusTree& operator=(BPlusTree&& other) noexcept {
        if (this != &other) {
            Clear();
            m_Order = other.m_Order;
            m_Size  = other.m_Size;
            m_Root  = other.m_Root;
            other.m_Size = 0;
            other.m_Root = nullptr;
        }
        return *this;
    }

    //==================================================
    // Basic Operations
    //==================================================

    void Insert(const KeyType& key, const RecordID& record){

        if (m_Root == nullptr){
            LeafNode<KeyType>* newLeaf = new LeafNode<KeyType>();
            newLeaf->keys.push_back(key);
            newLeaf->values.push_back({record});
            m_Root = newLeaf;
            m_Size++;
            return;
        }

        LeafNode<KeyType>* leaf = FindLeaf(key);
        InsertIntoLeaf(leaf, key, record);
    };

    // Removes a single record from the posting list of `key`.
    // The key itself disappears only once its posting list is empty.
    void Remove(const KeyType& key, const RecordID& record){
        LeafNode<KeyType>* leaf = FindLeaf(key);
        if (leaf == nullptr) {
            return;
        }

        std::size_t index = LowerBoundIndex(leaf->keys, key);
        if (index == leaf->keys.size() || key < leaf->keys[index]) {
            return;                              // key not present
        }

        std::vector<RecordID>& postings = leaf->values[index];
        for (std::size_t i = 0; i < postings.size(); ++i) {
            if (postings[i] == record) {
                postings.erase(postings.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }

        if (!postings.empty()) {
            return;                              // key survives, no structural change
        }

        leaf->keys.erase(leaf->keys.begin() + static_cast<std::ptrdiff_t>(index));
        leaf->values.erase(leaf->values.begin() + static_cast<std::ptrdiff_t>(index));
        m_Size--;

        // A stale separator upstairs is harmless: separators are routing values,
        // not data. A search for the removed key still lands on this leaf and
        // then fails the match check. Only merges and borrows must fix them.
        FixUnderflow(leaf);
    };

    bool Update(const KeyType& oldKey, const KeyType& newKey, const RecordID& record){
        if (!Contains(oldKey)) {
            return false;
        }
        Remove(oldKey, record);
        Insert(newKey, record);
        return true;
    };

    void Clear(){
        DestroyTree(m_Root);
        m_Root = nullptr;
        m_Size = 0;
    };

    //==================================================
    // Search
    //==================================================

    bool Contains(const KeyType& key) const {
        LeafNode<KeyType>* leaf = FindLeaf(key);

        if (leaf == nullptr) {
            return false;
        };

        std::size_t index = LowerBoundIndex(leaf->keys, key);

        if (index == leaf->keys.size() || key < leaf->keys[index]){
            return false;
        }

        return true;
    };

    std::vector<RecordID> Search(const KeyType& key) const {
        LeafNode<KeyType>* leaf = FindLeaf(key);

        if (leaf == nullptr) {
            return {};
        };

        std::size_t index = LowerBoundIndex(leaf->keys, key);

        if (index == leaf->keys.size() || key < leaf->keys[index]){
            return {};
        }

        return leaf->values[index];
    };

    // Inclusive on both ends: [lower, upper].
    // Descends once, then walks the leaf chain horizontally.
    std::vector<RecordID> RangeSearch(const KeyType& lower, const KeyType& upper) const {
        std::vector<RecordID> result;

        if (m_Root == nullptr || upper < lower) {
            return result;
        }

        LeafNode<KeyType>* leaf = FindLeaf(lower);
        std::size_t index = LowerBoundIndex(leaf->keys, lower);

        while (leaf != nullptr) {
            for (; index < leaf->keys.size(); ++index) {
                if (upper < leaf->keys[index]) {
                    return result;               // past the range, done
                }
                const std::vector<RecordID>& postings = leaf->values[index];
                result.insert(result.end(), postings.begin(), postings.end());
            }
            leaf = leaf->next;
            index = 0;
        }

        return result;
    };

    //==================================================
    // Tree Information
    //==================================================

    bool Empty() const {
        return m_Root == nullptr;
    };

    std::size_t Size() const {
        return m_Size;
    };

    // Number of levels. Empty tree is 0, a lone root leaf is 1.
    std::size_t Height() const {
        if (m_Root == nullptr) {
            return 0;
        }

        std::size_t height = 1;
        BPlusTreeNode<KeyType>* node = m_Root;

        while (!node->isLeaf) {
            node = static_cast<InternalNode<KeyType>*>(node)->children[0];
            height++;
        }

        return height;
    };

    std::size_t Order() const {
        return m_Order;
    };

    //==================================================
    // Debugging
    //==================================================

    void Print(std::ostream& out = std::cout) const {
        if (m_Root == nullptr) {
            out << "<empty tree>\n";
            return;
        }
        PrintNode(m_Root, 0, out);
    }

    // Walks the leaf chain left to right. Proves the `next` pointers are intact.
    void PrintLeafChain(std::ostream& out = std::cout) const {
        if (m_Root == nullptr) {
            out << "<empty tree>\n";
            return;
        }

        BPlusTreeNode<KeyType>* node = m_Root;
        while (!node->isLeaf) {
            node = static_cast<InternalNode<KeyType>*>(node)->children[0];
        }

        LeafNode<KeyType>* leaf = static_cast<LeafNode<KeyType>*>(node);
        out << "CHAIN: ";
        while (leaf != nullptr) {
            out << "[";
            for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
                if (i > 0) out << " ";
                out << leaf->keys[i];
            }
            out << "]";
            if (leaf->next != nullptr) out << " -> ";
            leaf = leaf->next;
        }
        out << "\n";
    }

    //==================================================
    // Validation
    //==================================================

    bool Validate() const {
        if (m_Root == nullptr) {
            return m_Size == 0;
        }

        if (m_Root->parent != nullptr) {
            return false;
        }

        std::size_t leafDepth = 0;
        bool leafDepthSet = false;
        std::size_t keyCount = 0;

        if (!ValidateNode(m_Root, nullptr, nullptr, 1, leafDepth, leafDepthSet, keyCount)) {
            return false;
        }

        if (keyCount != m_Size) {
            return false;
        }

        return ValidateLeafChain();
    };

    //==================================================
    // Access
    //==================================================

    BPlusTreeNode<KeyType>* Root() const {
        return m_Root;
    };

private:

    //==================================================
    // Capacity bounds, all derived from m_Order
    //==================================================

    std::size_t MaxKeys() const {
        return m_Order - 1;                      // both leaves and internal nodes
    }

    std::size_t MinLeafKeys() const {
        return m_Order / 2;                      // ceil((m - 1) / 2)
    }

    std::size_t MinInternalKeys() const {
        return (m_Order + 1) / 2 - 1;            // ceil(m / 2) - 1
    }

    //==================================================
    // Search Helpers
    //==================================================

    LeafNode<KeyType>* FindLeaf(const KeyType& key) const{
        if (m_Root == nullptr) {
            return nullptr;
        };

        BPlusTreeNode<KeyType>* currentNode = m_Root;

        while(!currentNode->isLeaf){
            InternalNode<KeyType>* internalNode = static_cast<InternalNode<KeyType>*>(currentNode);

            std::size_t index = UpperBoundIndex(internalNode->keys, key);
            currentNode = internalNode->children[index];
        }

        return static_cast<LeafNode<KeyType>*>(currentNode);
    };

    std::size_t UpperBoundIndex(const std::vector<KeyType>& keys, const KeyType& key) const{
        int right = static_cast<int>(keys.size()) - 1;
        int left = 0;

        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (!(key < keys[mid])){
                left = mid + 1;
            }else{
                right = mid - 1;
            }
        }

        return left;
    }

    std::size_t LowerBoundIndex(const std::vector<KeyType>& keys, const KeyType& key) const {
        int left = 0;
        int right = static_cast<int>(keys.size()) - 1;

        while (left <= right){
            int mid = left + (right - left) / 2;

            if (keys[mid] < key){
                left = mid + 1;
            }else{
                right = mid - 1;
            };
        }

        return left;
    }

    InternalNode<KeyType>* FindParent(BPlusTreeNode<KeyType>* node) const {
        return static_cast<InternalNode<KeyType>*>(node->parent);
    };

    // Position of `child` inside its parent's children vector.
    std::size_t ChildIndex(InternalNode<KeyType>* parent,
                           BPlusTreeNode<KeyType>* child) const {
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i] == child) {
                return i;
            }
        }
        return parent->children.size();          // not found
    }

    //==================================================
    // Insert Helpers
    //==================================================

    void InsertIntoLeaf(LeafNode<KeyType>* leaf, const KeyType& key, const RecordID& record){
        std::size_t index = LowerBoundIndex(leaf->keys, key);

        if (index < leaf->keys.size() && !(key < leaf->keys[index])) {
            // Key already exists, append the record to the existing posting list
            leaf->values[index].push_back(record);
            return;
        }

        leaf->keys.insert(leaf->keys.begin() + static_cast<std::ptrdiff_t>(index), key);
        leaf->values.insert(leaf->values.begin() + static_cast<std::ptrdiff_t>(index), {record});
        m_Size++;

        // Transient overflow is allowed: we inserted first, we split second.
        if (leaf->keys.size() > MaxKeys()) {
            SplitLeaf(leaf);
        }
    };

    void SplitLeaf(LeafNode<KeyType>* leaf){
        const std::size_t total = leaf->keys.size();
        const std::size_t splitPoint = (total + 1) / 2;

        LeafNode<KeyType>* newLeaf = new LeafNode<KeyType>();

        newLeaf->keys.assign(leaf->keys.begin() + static_cast<std::ptrdiff_t>(splitPoint),
                             leaf->keys.end());
        newLeaf->values.assign(leaf->values.begin() + static_cast<std::ptrdiff_t>(splitPoint),
                               leaf->values.end());

        leaf->keys.erase(leaf->keys.begin() + static_cast<std::ptrdiff_t>(splitPoint),
                         leaf->keys.end());
        leaf->values.erase(leaf->values.begin() + static_cast<std::ptrdiff_t>(splitPoint),
                           leaf->values.end());

        // Splice into the leaf chain.
        newLeaf->next = leaf->next;
        newLeaf->previous = leaf;
        if (leaf->next != nullptr) {
            leaf->next->previous = newLeaf;
        }
        leaf->next = newLeaf;

        // The separator is COPIED up, not moved: the key stays in the leaf,
        // because leaves hold the data. (Internal splits move it instead.)
        InsertIntoParent(leaf, newLeaf->keys[0], newLeaf);
    };

    void InsertIntoParent(BPlusTreeNode<KeyType>* left,
                          const KeyType& key,
                          BPlusTreeNode<KeyType>* right){

        if (left == m_Root) {
            InternalNode<KeyType>* newRoot = new InternalNode<KeyType>();
            newRoot->keys.push_back(key);
            newRoot->children.push_back(left);
            newRoot->children.push_back(right);

            left->parent = newRoot;
            right->parent = newRoot;
            m_Root = newRoot;
            return;
        }

        InternalNode<KeyType>* parent = FindParent(left);
        const std::size_t index = ChildIndex(parent, left);

        parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(index), key);
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                right);
        right->parent = parent;

        if (parent->keys.size() > MaxKeys()) {
            SplitInternal(parent);
        }
    };

    void SplitInternal(InternalNode<KeyType>* node){
        const std::size_t total = node->keys.size();
        const std::size_t mid = total / 2;

        const KeyType separator = node->keys[mid];

        InternalNode<KeyType>* newNode = new InternalNode<KeyType>();

        newNode->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(mid + 1),
                             node->keys.end());
        newNode->children.assign(node->children.begin() + static_cast<std::ptrdiff_t>(mid + 1),
                                 node->children.end());

        for (BPlusTreeNode<KeyType>* child : newNode->children) {
            child->parent = newNode;
        }

        // keys[mid] MOVES up; it is not left behind. An internal key is only a
        // signpost, and the parent is now the one pointing at that boundary.
        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(mid),
                         node->keys.end());
        node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(mid + 1),
                             node->children.end());

        InsertIntoParent(node, separator, newNode);
    };

    //==================================================
    // Delete Helpers
    //==================================================

    void FixUnderflow(BPlusTreeNode<KeyType>* node){

        if (node == m_Root) {
            ShrinkRoot();
            return;
        }

        const std::size_t minimum = node->isLeaf ? MinLeafKeys() : MinInternalKeys();
        if (node->keys.size() >= minimum) {
            return;
        }

        InternalNode<KeyType>* parent = FindParent(node);
        const std::size_t index = ChildIndex(parent, node);

        BPlusTreeNode<KeyType>* leftSibling =
            (index > 0) ? parent->children[index - 1] : nullptr;
        BPlusTreeNode<KeyType>* rightSibling =
            (index + 1 < parent->children.size()) ? parent->children[index + 1] : nullptr;

        if (leftSibling != nullptr && leftSibling->keys.size() > minimum) {
            BorrowFromLeft(node);
            return;
        }

        if (rightSibling != nullptr && rightSibling->keys.size() > minimum) {
            BorrowFromRight(node);
            return;
        }

        // No sibling can spare a key, so merge. Always merge into the left of
        // the pair, so the surviving node keeps its position in the parent.
        if (leftSibling != nullptr) {
            MergeNodes(leftSibling, node);
        } else {
            MergeNodes(node, rightSibling);
        }

        FixUnderflow(parent);
    };

    void BorrowFromLeft(BPlusTreeNode<KeyType>* node){
        InternalNode<KeyType>* parent = FindParent(node);
        const std::size_t index = ChildIndex(parent, node);
        BPlusTreeNode<KeyType>* leftSibling = parent->children[index - 1];

        if (node->isLeaf) {
            LeafNode<KeyType>* target = static_cast<LeafNode<KeyType>*>(node);
            LeafNode<KeyType>* source = static_cast<LeafNode<KeyType>*>(leftSibling);

            target->keys.insert(target->keys.begin(), source->keys.back());
            target->values.insert(target->values.begin(), source->values.back());

            source->keys.pop_back();
            source->values.pop_back();

            parent->keys[index - 1] = target->keys[0];
            return;
        }

        InternalNode<KeyType>* target = static_cast<InternalNode<KeyType>*>(node);
        InternalNode<KeyType>* source = static_cast<InternalNode<KeyType>*>(leftSibling);

        // Rotate through the parent: the separator drops into the child, and the
        // sibling's outermost key rises to replace it.
        target->keys.insert(target->keys.begin(), parent->keys[index - 1]);
        target->children.insert(target->children.begin(), source->children.back());
        target->children.front()->parent = target;

        parent->keys[index - 1] = source->keys.back();

        source->keys.pop_back();
        source->children.pop_back();
    };

    void BorrowFromRight(BPlusTreeNode<KeyType>* node){
        InternalNode<KeyType>* parent = FindParent(node);
        const std::size_t index = ChildIndex(parent, node);
        BPlusTreeNode<KeyType>* rightSibling = parent->children[index + 1];

        if (node->isLeaf) {
            LeafNode<KeyType>* target = static_cast<LeafNode<KeyType>*>(node);
            LeafNode<KeyType>* source = static_cast<LeafNode<KeyType>*>(rightSibling);

            target->keys.push_back(source->keys.front());
            target->values.push_back(source->values.front());

            source->keys.erase(source->keys.begin());
            source->values.erase(source->values.begin());

            parent->keys[index] = source->keys[0];
            return;
        }

        InternalNode<KeyType>* target = static_cast<InternalNode<KeyType>*>(node);
        InternalNode<KeyType>* source = static_cast<InternalNode<KeyType>*>(rightSibling);

        target->keys.push_back(parent->keys[index]);
        target->children.push_back(source->children.front());
        target->children.back()->parent = target;

        parent->keys[index] = source->keys.front();

        source->keys.erase(source->keys.begin());
        source->children.erase(source->children.begin());
    };

    // Folds `right` into `left`. They must be adjacent children of one parent.
    // Deletes `right` and removes the separator that divided them.
    void MergeNodes(BPlusTreeNode<KeyType>* left, BPlusTreeNode<KeyType>* right){
        InternalNode<KeyType>* parent = FindParent(left);
        const std::size_t rightIndex = ChildIndex(parent, right);

        if (left->isLeaf) {
            LeafNode<KeyType>* leftLeaf = static_cast<LeafNode<KeyType>*>(left);
            LeafNode<KeyType>* rightLeaf = static_cast<LeafNode<KeyType>*>(right);

            leftLeaf->keys.insert(leftLeaf->keys.end(),
                                  rightLeaf->keys.begin(), rightLeaf->keys.end());
            leftLeaf->values.insert(leftLeaf->values.end(),
                                    rightLeaf->values.begin(), rightLeaf->values.end());

            // Unlink from the leaf chain.
            leftLeaf->next = rightLeaf->next;
            if (rightLeaf->next != nullptr) {
                rightLeaf->next->previous = leftLeaf;
            }
        } else {
            InternalNode<KeyType>* leftInternal = static_cast<InternalNode<KeyType>*>(left);
            InternalNode<KeyType>* rightInternal = static_cast<InternalNode<KeyType>*>(right);

            // The separator comes DOWN from the parent to sit between the two
            // key runs. Without it the merged node would be missing a boundary.
            leftInternal->keys.push_back(parent->keys[rightIndex - 1]);
            leftInternal->keys.insert(leftInternal->keys.end(),
                                      rightInternal->keys.begin(), rightInternal->keys.end());

            leftInternal->children.insert(leftInternal->children.end(),
                                          rightInternal->children.begin(),
                                          rightInternal->children.end());

            for (BPlusTreeNode<KeyType>* child : rightInternal->children) {
                child->parent = leftInternal;
            }
        }

        parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(rightIndex - 1));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(rightIndex));

        delete right;
    };

    // The root is the only node allowed to be under-full. It collapses when it
    // has no keys left: either the tree empties, or its lone child takes over.
    void ShrinkRoot(){
        if (m_Root == nullptr || !m_Root->keys.empty()) {
            return;
        }

        if (m_Root->isLeaf) {
            delete m_Root;
            m_Root = nullptr;
            return;
        }

        InternalNode<KeyType>* oldRoot = static_cast<InternalNode<KeyType>*>(m_Root);
        m_Root = oldRoot->children[0];
        m_Root->parent = nullptr;

        oldRoot->children.clear();
        delete oldRoot;
    };

    //==================================================
    // Utility
    //==================================================

    void DestroyTree(BPlusTreeNode<KeyType>* node){
        if (node == nullptr) {
            return;
        }

        if (!node->isLeaf) {
            InternalNode<KeyType>* internal = static_cast<InternalNode<KeyType>*>(node);
            for (BPlusTreeNode<KeyType>* child : internal->children) {
                DestroyTree(child);
            }
        }

        delete node;
    };

    //==================================================
    // Validation Helpers
    //==================================================

    // lowerBound is inclusive, upperBound exclusive; null means unbounded.
    bool ValidateNode(BPlusTreeNode<KeyType>* node,
                      const KeyType* lowerBound,
                      const KeyType* upperBound,
                      std::size_t depth,
                      std::size_t& leafDepth,
                      bool& leafDepthSet,
                      std::size_t& keyCount) const {

        // Keys must be strictly increasing inside every node.
        for (std::size_t i = 1; i < node->keys.size(); ++i) {
            if (!(node->keys[i - 1] < node->keys[i])) {
                return false;
            }
        }

        // Every key must sit inside the window the separators promised.
        for (const KeyType& key : node->keys) {
            if (lowerBound != nullptr && key < *lowerBound) {
                return false;
            }
            if (upperBound != nullptr && !(key < *upperBound)) {
                return false;
            }
        }

        if (node->keys.size() > MaxKeys()) {
            return false;
        }

        if (node->isLeaf) {
            LeafNode<KeyType>* leaf = static_cast<LeafNode<KeyType>*>(node);

            if (leaf->keys.size() != leaf->values.size()) {
                return false;
            }

            for (const std::vector<RecordID>& postings : leaf->values) {
                if (postings.empty()) {
                    return false;                // a key with no records must not exist
                }
            }

            if (node != m_Root && leaf->keys.size() < MinLeafKeys()) {
                return false;
            }

            // All leaves must sit at the same depth.
            if (!leafDepthSet) {
                leafDepth = depth;
                leafDepthSet = true;
            } else if (depth != leafDepth) {
                return false;
            }

            keyCount += leaf->keys.size();
            return true;
        }

        InternalNode<KeyType>* internal = static_cast<InternalNode<KeyType>*>(node);

        // n keys means exactly n + 1 children. This is the load-bearing invariant.
        if (internal->children.size() != internal->keys.size() + 1) {
            return false;
        }

        if (node == m_Root) {
            if (internal->children.size() < 2) {
                return false;
            }
        } else if (internal->keys.size() < MinInternalKeys()) {
            return false;
        }

        for (std::size_t i = 0; i < internal->children.size(); ++i) {
            BPlusTreeNode<KeyType>* child = internal->children[i];

            if (child == nullptr || child->parent != internal) {
                return false;
            }

            const KeyType* childLower = (i == 0) ? lowerBound : &internal->keys[i - 1];
            const KeyType* childUpper = (i == internal->keys.size())
                                            ? upperBound
                                            : &internal->keys[i];

            if (!ValidateNode(child, childLower, childUpper,
                              depth + 1, leafDepth, leafDepthSet, keyCount)) {
                return false;
            }
        }

        return true;
    }

    bool ValidateLeafChain() const {
        BPlusTreeNode<KeyType>* node = m_Root;
        while (!node->isLeaf) {
            node = static_cast<InternalNode<KeyType>*>(node)->children[0];
        }

        LeafNode<KeyType>* leaf = static_cast<LeafNode<KeyType>*>(node);
        if (leaf->previous != nullptr) {
            return false;
        }

        while (leaf->next != nullptr) {
            if (leaf->next->previous != leaf) {
                return false;                    // links must agree in both directions
            }
            if (leaf->keys.empty() || leaf->next->keys.empty()) {
                return false;
            }
            if (!(leaf->keys.back() < leaf->next->keys.front())) {
                return false;                    // chain must be globally sorted
            }
            leaf = leaf->next;
        }

        return true;
    }

    void PrintNode(BPlusTreeNode<KeyType>* node, int depth, std::ostream& out) const {
        const std::string indent(depth * 2, ' ');

        if (node->isLeaf) {
            LeafNode<KeyType>* leaf = static_cast<LeafNode<KeyType>*>(node);

            out << indent << "LEAF [";
            for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
                if (i > 0) out << ", ";
                out << leaf->keys[i] << "(" << leaf->values[i].size() << ")";
            }
            out << "]\n";
            return;
        }

        InternalNode<KeyType>* internal = static_cast<InternalNode<KeyType>*>(node);

        out << indent << "NODE [";
        for (std::size_t i = 0; i < internal->keys.size(); ++i) {
            if (i > 0) out << ", ";
            out << internal->keys[i];
        }
        out << "]\n";

        for (BPlusTreeNode<KeyType>* child : internal->children) {
            if (child == nullptr) {
                out << indent << "  <null child>\n";
                continue;
            }
            PrintNode(child, depth + 1, out);
        }
    }

private:

    std::size_t m_Order = 0;

    std::size_t m_Size = 0;

    BPlusTreeNode<KeyType>* m_Root = nullptr;
};
