#pragma once
#include <string>
#include <iostream>
#include "../utils/logger.hpp"

class TreeNode {
public:
    std::string value;
    TreeNode* leftChild = nullptr;
    TreeNode* rightChild = nullptr;

    TreeNode(std::string val) : value(std::move(val)) {}
};

class BSTree {
public:
    ~BSTree() {
        clear(root);
    }

    void insert(const std::string& value) {
        recurseInsertion(root, value);
    }

    TreeNode* search(const std::string& word) {
        return recurseSearch(root, word);
    }

    void remove(const std::string& word) {
        recurseRemove(word);
    }

    void print() {
        traverseInOrder(root);
    }

private:
    TreeNode* root = nullptr;

    void clear(TreeNode* node) {
        if (node) {
            clear(node->leftChild);
            clear(node->rightChild);
            delete node;
        }
    }

    bool recurseInsertion(TreeNode*& node, const std::string& value) {
        if (!node) {
            node = new TreeNode(value);
            return true;
        }
        if (value < node->value) {
            recurseInsertion(node->leftChild, value);
        } else if (value > node->value) {
            recurseInsertion(node->rightChild, value);
        }
        return true;
    }

    TreeNode* recurseSearch(TreeNode* node, const std::string& value) {
        if (!node || node->value == value) {
            return node;
        }
        if (value < node->value) {
            return recurseSearch(node->leftChild, value);
        } else {
            return recurseSearch(node->rightChild, value);
        }
    }

    TreeNode*& getNodeRef(TreeNode*& node, const std::string& value) {
        if (!node) return node;
        if (node->value == value) return node;
        if (value < node->value) {
            return getNodeRef(node->leftChild, value);
        } else {
            return getNodeRef(node->rightChild, value);
        }
    }

    TreeNode*& findMin(TreeNode*& node) {
        if (!node->leftChild) return node;
        return findMin(node->leftChild);
    }

    void recurseRemove(const std::string& value) {
        TreeNode*& target = getNodeRef(root, value);
        if (!target) return;

        if (!target->leftChild && !target->rightChild) {
            delete target;
            target = nullptr;
        } else if (!target->leftChild) {
            TreeNode* temp = target;
            target = target->rightChild;
            delete temp;
        } else if (!target->rightChild) {
            TreeNode* temp = target;
            target = target->leftChild;
            delete temp;
        } else {
            TreeNode*& successor = findMin(target->rightChild);
            target->value = successor->value;
            TreeNode* temp = successor;
            successor = successor->rightChild;
            delete temp;
        }
    }

    void traverseInOrder(TreeNode* node) {
        if (!node) return;
        traverseInOrder(node->leftChild);
        Log("BSTree", node->value);
        traverseInOrder(node->rightChild);
    }
};