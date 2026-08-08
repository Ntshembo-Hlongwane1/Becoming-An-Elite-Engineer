#pragma once
#include "BPlusTreeNode.hpp"

template<typename KeyType>
class InternalNode : public BPlusTreeNode<KeyType> {
    public:
        InternalNode(){
            this->isLeaf = false;
        }

    public:
        std::vector<BPlusTreeNode<KeyType>*> children;
};