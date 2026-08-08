#pragma once

#include "BPlusTreeNode.hpp"
#include "RecordID.hpp"

template<typename KeyType>
class LeafNode : public BPlusTreeNode<KeyType>{
    public:
        LeafNode(){
            this->isLeaf = true;
        }

    public:

        // One posting list per key
        std::vector<std::vector<RecordID>> values;

        LeafNode<KeyType>* next = nullptr;
        LeafNode<KeyType>* previous = nullptr;
};