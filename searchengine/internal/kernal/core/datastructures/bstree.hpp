#pragma once
#include <string>

class TreeNode {
    public:
        std::string value;
        TreeNode* leftChild{nullptr};
        TreeNode* rightChild{nullptr};

        TreeNode(std::string val) : value(std::move(val)) {}
};

class BSTree {
    public:
        void insert(TreeNode*& root, std::string value){
            if (!root){
                root = new TreeNode(value);
                return;
            }

            if (value < root->value){
                insert(root->leftChild, value);
            } else if (value > root->value){
                insert(root->rightChild, value);
            }
        }
        void search(std::string& word);
        void remove(std::string& word);
        void print();
};