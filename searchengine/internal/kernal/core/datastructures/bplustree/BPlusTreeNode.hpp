#pragma once
#include <vector>

template<typename KeyType>
class BPlusTreeNode {
    public:
        virtual ~BPlusTreeNode() = default;

        bool isLeaf = false;

        std::vector<KeyType> keys;

        BPlusTreeNode<KeyType>* parent = nullptr;
};