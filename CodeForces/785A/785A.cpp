#include <iostream>
#include <unordered_map>
#include <string>

int main() {

    int n;

    std::unordered_map<std::string, int> shapes = {
        {"Tetrahedron", 4},
        {"Cube", 6},
        {"Octahedron", 8},
        {"Dodecahedron", 12},
        {"Icosahedron", 20}
    };

    int total_faces = 0;

    std::cin >> n;

    for (int i = 0; i < n; i++) {
        std::string shape;
        std::cin >> shape;
        total_faces += shapes[shape];
    }

    std::cout << total_faces << std::endl;
}