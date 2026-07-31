#include <iostream>
#include <cassert>
#include "../bstree.hpp"
#include "../../utils/logger.hpp"

void test_insert_and_search() {
    Log("BSTree Test", "Running test_insert_and_search...");
    BSTree tree;

    tree.insert("banana");
    tree.insert("apple");
    tree.insert("cherry");

    TreeNode* res1 = tree.search("apple");
    assert(res1 != nullptr);
    assert(res1->value == "apple");

    TreeNode* res2 = tree.search("banana");
    assert(res2 != nullptr);
    assert(res2->value == "banana");

    TreeNode* res3 = tree.search("cherry");
    assert(res3 != nullptr);
    assert(res3->value == "cherry");

    Log("BSTree Test", "  [PASSED] Insert and Search");
}

void test_search_not_found() {
    Log("BSTree Test", "Running test_search_not_found...");
    BSTree tree;

    tree.insert("mango");
    tree.insert("grape");

    TreeNode* res = tree.search("orange");
    assert(res == nullptr);

    Log("BSTree Test", "  [PASSED] Search Not Found");
}

void test_remove() {
    Log("BSTree Test", "Running test_remove...");
    BSTree tree;

    tree.insert("dog");
    tree.insert("cat");
    tree.insert("elephant");

    assert(tree.search("cat") != nullptr);

    tree.remove("cat");
    assert(tree.search("cat") == nullptr);
    assert(tree.search("dog") != nullptr);
    assert(tree.search("elephant") != nullptr);

    Log("BSTree Test", "  [PASSED] Remove Node");
}

void test_print_inorder() {
    Log("BSTree Test", "Running test_print_inorder...");
    BSTree tree;

    tree.insert("50");
    tree.insert("30");
    tree.insert("70");
    tree.insert("20");
    tree.insert("40");

    Log("BSTree Test", "--- In-order traversal output ---");
    tree.print();
    Log("BSTree Test", "---------------------------------");

    Log("BSTree Test", "  [PASSED] Print In-order");
}

int main() {
    Log("BSTree Test", "========================================");
    Log("BSTree Test", "        BSTree Unit Test Suite         ");
    Log("BSTree Test", "========================================");

    test_insert_and_search();
    test_search_not_found();
    test_remove();
    test_print_inorder();

    Log("BSTree Test", "All BSTree tests passed successfully!");
    return 0;
}
