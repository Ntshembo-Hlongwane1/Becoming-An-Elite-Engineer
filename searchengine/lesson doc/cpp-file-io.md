# C++ File I/O — The Full Picture (For Go Developers)

> **Goal**: After reading this, you should never need to ask "how do I work with files in C++?" again.  
> Every concept is mapped back to Go so you can build on what you already know.

---

## Table of Contents

1. [The Mental Model Shift](#1-the-mental-model-shift)
2. [Streams — The Core Abstraction](#2-streams--the-core-abstraction)
3. [Opening & Closing Files](#3-opening--closing-files)
4. [Reading Files](#4-reading-files)
5. [Writing Files](#5-writing-files)
6. [Error Handling](#6-error-handling)
7. [The Filesystem Library — Listing Directories](#7-the-filesystem-library--listing-directories)
8. [Putting It All Together — Real Patterns](#8-putting-it-all-together--real-patterns)
9. [Quick Reference Cheat Sheet](#9-quick-reference-cheat-sheet)

---

## 1. The Mental Model Shift

In **Go**, file I/O is built around simple interfaces:

```go
file, err := os.Open("data.txt")   // returns *os.File, error
defer file.Close()                  // you manually defer closing
data, err := io.ReadAll(file)       // read everything
```

In **C++**, file I/O is built around **streams** — objects that represent a flow of data.  
The biggest difference: **C++ uses RAII (Resource Acquisition Is Initialization)** — meaning the file automatically closes when the stream object goes out of scope. No `defer` needed.

```cpp
{
    std::ifstream file("data.txt");       // file opens HERE
    std::string content;
    // ... read from file ...
}   // file closes HERE automatically — the destructor runs
```

### Key Vocabulary Mapping

| Go Concept | C++ Equivalent | Header |
|---|---|---|
| `os.Open()` | `std::ifstream` (input file stream) | `<fstream>` |
| `os.Create()` | `std::ofstream` (output file stream) | `<fstream>` |
| `os.OpenFile()` | `std::fstream` (read + write) | `<fstream>` |
| `defer f.Close()` | Automatic (destructor / RAII) | — |
| `io.ReadAll()` | `std::istreambuf_iterator` or `getline()` | `<fstream>`, `<iterator>` |
| `fmt.Fprintf()` | `<<` operator on stream | `<fstream>` |
| `os.ReadDir()` | `std::filesystem::directory_iterator` | `<filesystem>` |
| `filepath.Walk()` | `std::filesystem::recursive_directory_iterator` | `<filesystem>` |
| `err != nil` | `file.is_open()`, `file.fail()`, `file.eof()` | `<fstream>` |

---

## 2. Streams — The Core Abstraction

Before touching files, you need to understand **streams** because everything in C++ I/O is a stream.

### What is a stream?

A stream is an object that represents a **sequence of bytes** flowing in one direction.  
Think of it like a pipe:

```
[Source] ---bytes---> [Your Program]     ← this is an INPUT stream  (std::ifstream)
[Your Program] ---bytes---> [Destination] ← this is an OUTPUT stream (std::ofstream)
```

You already use streams without realising it:

```cpp
std::cout << "Hello";   // cout IS a stream — it flows bytes to your terminal
std::cin >> x;          // cin IS a stream  — it flows bytes from your keyboard
```

### The stream class hierarchy

```
                    std::ios_base          ← base settings (formatting flags, etc.)
                         |
                     std::ios              ← error state flags (good, fail, eof, bad)
                    /         \
           std::istream    std::ostream    ← input / output base classes
                |      \  /      |
                |    std::iostream |
                |         |       |
          std::ifstream  std::fstream  std::ofstream
          (read file)   (read+write)   (write file)
```

**You only need to care about 3 classes:**

| Class | Direction | Go Equivalent | When to use |
|---|---|---|---|
| `std::ifstream` | Read only | `os.Open()` | Reading data files |
| `std::ofstream` | Write only | `os.Create()` | Writing/creating files |
| `std::fstream` | Read + Write | `os.OpenFile()` | Rare — when you need both |

---

## 3. Opening & Closing Files

### Opening a file

There are two ways — both are equivalent:

```cpp
// Way 1: Open in the constructor (preferred — clean and simple)
std::ifstream file("data/myfile.txt");

// Way 2: Create first, open later (useful for conditional logic)
std::ifstream file;
file.open("data/myfile.txt");
```

**Go equivalent:**
```go
file, err := os.Open("data/myfile.txt")
```

### Always check if it opened

```cpp
std::ifstream file("data/myfile.txt");

if (!file.is_open()) {
    std::cerr << "Failed to open file!" << std::endl;
    return;
}
```

**Why `!file.is_open()` instead of checking an error?**  
In Go, `os.Open` returns an `error`. In C++, the stream object itself tracks its state internally. There is no separate error value — you ask the stream "are you okay?"

### Closing a file

```cpp
file.close();   // explicit close — you CAN do this
```

**But you almost never need to.** When `file` goes out of scope (the `}` brace), its **destructor** runs automatically and closes it. This is RAII:

```cpp
void ReadSomething() {
    std::ifstream file("data.txt");   // OPENS here
    // ... use file ...
}   // CLOSES here automatically — destructor called
```

**Go comparison:**
```go
func ReadSomething() {
    file, _ := os.Open("data.txt")
    defer file.Close()   // you have to remember this!
    // ... use file ...
}
```

C++ advantage: you literally cannot forget to close the file. The language does it for you.

### Open modes (flags)

When opening a file, you can specify **how** to open it using flags:

```cpp
// Read only (default for ifstream)
std::ifstream file("data.txt", std::ios::in);

// Write only, truncate existing content (default for ofstream)
std::ofstream file("data.txt", std::ios::out);

// Append to end of file
std::ofstream file("log.txt", std::ios::app);

// Binary mode (no text conversions)
std::ifstream file("image.bin", std::ios::binary);

// Combine flags with |
std::fstream file("data.txt", std::ios::in | std::ios::out | std::ios::binary);
```

| Flag | Meaning | Go Equivalent |
|---|---|---|
| `std::ios::in` | Open for reading | `os.O_RDONLY` |
| `std::ios::out` | Open for writing (truncates!) | `os.O_WRONLY \| os.O_TRUNC` |
| `std::ios::app` | Append to end | `os.O_APPEND` |
| `std::ios::trunc` | Truncate file to zero length | `os.O_TRUNC` |
| `std::ios::binary` | Binary mode (no `\r\n` conversion) | — (Go doesn't convert) |
| `std::ios::ate` | Seek to end after opening | — |

---

## 4. Reading Files

### Method 1: Read line by line with `std::getline()`

This is the most common pattern. Like Go's `bufio.Scanner`:

```cpp
#include <fstream>
#include <iostream>
#include <string>

std::ifstream file("data/myfile.txt");

std::string line;
while (std::getline(file, line)) {
    std::cout << line << std::endl;
}
```

**How it works under the hood:**
1. `std::getline(file, line)` reads characters from the stream until it hits `\n`
2. It stores everything (excluding the `\n`) into `line`
3. It returns a reference to the stream itself
4. When used in a `while` condition, the stream converts to `bool` — `true` if stream is good, `false` if EOF or error

**Go equivalent:**
```go
scanner := bufio.NewScanner(file)
for scanner.Scan() {
    fmt.Println(scanner.Text())
}
```

### Method 2: Read word by word with `>>`

The `>>` operator reads whitespace-delimited tokens:

```cpp
std::ifstream file("data.txt");

std::string word;
while (file >> word) {
    std::cout << "Word: " << word << std::endl;
}
```

This splits on **any whitespace** (spaces, tabs, newlines). Like Go's `fmt.Fscan`.

### Method 3: Read entire file into a string

```cpp
#include <fstream>
#include <string>
#include <iterator>

std::ifstream file("data/myfile.txt");

std::string content(
    (std::istreambuf_iterator<char>(file)),   // start: first byte of file
    std::istreambuf_iterator<char>()           // end: EOF sentinel
);
```

**Breaking this down piece by piece:**

- `std::istreambuf_iterator<char>(file)` — creates an iterator pointing to the FIRST byte of the file's internal buffer
- `std::istreambuf_iterator<char>()` — creates a default/end iterator (like a sentinel that means "end of stream")
- `std::string(begin, end)` — the string constructor that takes two iterators and copies everything between them

**The extra parentheses around the first argument are required!** Without them, the compiler thinks you're declaring a function, not a variable. This is called the "Most Vexing Parse" — a famous C++ gotcha.

**Go equivalent:**
```go
data, err := io.ReadAll(file)
content := string(data)
```

### Method 4: Read character by character with `get()`

For maximum control:

```cpp
std::ifstream file("data.txt");

char ch;
while (file.get(ch)) {
    // process one character at a time
    std::cout << ch;
}
```

**Go equivalent:**
```go
reader := bufio.NewReader(file)
for {
    ch, err := reader.ReadByte()
    if err != nil { break }
    fmt.Print(string(ch))
}
```

### Method 5: Read a fixed number of bytes with `read()`

For binary data or when you want a specific chunk size:

```cpp
std::ifstream file("data.bin", std::ios::binary);

char buffer[1024];                     // 1KB buffer
file.read(buffer, sizeof(buffer));     // read up to 1024 bytes

// How many bytes were actually read?
std::streamsize bytesRead = file.gcount();
```

**Go equivalent:**
```go
buffer := make([]byte, 1024)
n, err := file.Read(buffer)
```

---

## 5. Writing Files

### Method 1: Write with `<<` operator

```cpp
#include <fstream>
#include <string>

std::ofstream file("output.txt");

file << "Hello, World!" << std::endl;
file << "Number: " << 42 << std::endl;
file << "Pi: " << 3.14159 << std::endl;
```

`<<` works exactly like it does with `std::cout` — because `cout` and `ofstream` are both output streams.

**Go equivalent:**
```go
file, _ := os.Create("output.txt")
fmt.Fprintln(file, "Hello, World!")
fmt.Fprintf(file, "Number: %d\n", 42)
```

### Method 2: Write raw bytes with `write()`

```cpp
std::ofstream file("output.bin", std::ios::binary);

const char* data = "raw bytes here";
file.write(data, 14);   // write exactly 14 bytes
```

**Go equivalent:**
```go
file.Write([]byte("raw bytes here"))
```

### Method 3: Append to a file

```cpp
std::ofstream file("log.txt", std::ios::app);   // app = append
file << "New log entry" << std::endl;
// existing content is preserved, new content goes at the end
```

**Go equivalent:**
```go
file, _ := os.OpenFile("log.txt", os.O_APPEND|os.O_WRONLY, 0644)
fmt.Fprintln(file, "New log entry")
```

### `std::endl` vs `"\n"`

```cpp
file << "line 1" << std::endl;   // writes '\n' AND flushes the buffer to disk
file << "line 2" << "\n";        // writes '\n' only — stays in memory buffer
```

- `std::endl` = newline + **flush** (forces the data to actually be written to disk NOW)
- `"\n"` = newline only (data stays in a memory buffer, written later)

**Performance tip**: Use `"\n"` in loops. `std::endl` forces a disk write on every line, which is very slow with hundreds of files. The buffer will flush automatically when the file closes.

```cpp
// SLOW — flushes to disk on every iteration
for (int i = 0; i < 10000; i++) {
    file << "line " << i << std::endl;
}

// FAST — buffers in memory, flushes once at the end
for (int i = 0; i < 10000; i++) {
    file << "line " << i << "\n";
}
```

---

## 6. Error Handling

In Go, every I/O operation returns an `error`. In C++, the **stream itself** carries error state.

### Stream state flags

Every stream has 4 internal flags:

| Flag | Method | Meaning |
|---|---|---|
| **goodbit** | `file.good()` | Everything is fine, no errors |
| **eofbit** | `file.eof()` | Reached end of file |
| **failbit** | `file.fail()` | Operation failed (wrong format, can't open, etc.) |
| **badbit** | `file.bad()` | Serious error (disk failure, corrupted stream) |

### Checking for errors

```cpp
std::ifstream file("data.txt");

// Check 1: Did it open?
if (!file.is_open()) {
    std::cerr << "Cannot open file" << std::endl;
}

// Check 2: After reading — was the read successful?
std::string line;
if (!std::getline(file, line)) {
    if (file.eof()) {
        std::cout << "Reached end of file" << std::endl;
    } else if (file.fail()) {
        std::cerr << "Read failed" << std::endl;
    } else if (file.bad()) {
        std::cerr << "Critical stream error" << std::endl;
    }
}
```

### The boolean conversion trick

Streams implicitly convert to `bool` — this is why `while (getline(file, line))` works:

```cpp
// These are equivalent:
while (std::getline(file, line))    // stream converts to true if good
while (!file.fail())                // explicitly checking fail flag
```

When the stream hits EOF or an error, it becomes "false" and the loop stops.

### Go vs C++ error handling comparison

```go
// Go: errors are values you check after every call
data, err := io.ReadAll(file)
if err != nil {
    log.Fatal(err)
}
```

```cpp
// C++: the stream remembers errors — you check the stream state
std::string content(
    (std::istreambuf_iterator<char>(file)),
    std::istreambuf_iterator<char>()
);

if (file.fail() && !file.eof()) {
    std::cerr << "Read error occurred" << std::endl;
}
```

---

## 7. The Filesystem Library — Listing Directories

This is where C++17 shines. The `<filesystem>` library is C++'s answer to Go's `os` and `filepath` packages.

**Required header and namespace alias:**
```cpp
#include <filesystem>

namespace fs = std::filesystem;   // shorthand — like Go's import alias
```

### List files in a directory

```cpp
for (const auto& entry : fs::directory_iterator("data")) {
    std::cout << entry.path() << std::endl;
}
```

**Breaking this down:**

- `fs::directory_iterator("data")` — creates an iterator that walks through the `data/` directory
- `entry` — each entry is a `fs::directory_entry` object (like Go's `os.DirEntry`)
- `entry.path()` — returns a `fs::path` object representing the full path

**Go equivalent:**
```go
entries, _ := os.ReadDir("data")
for _, entry := range entries {
    fmt.Println(entry.Name())
}
```

### What you can ask a `directory_entry`

```cpp
for (const auto& entry : fs::directory_iterator("data")) {
    // Is it a regular file (not a directory, symlink, etc.)?
    entry.is_regular_file();       // Go: !entry.IsDir()

    // Is it a directory?
    entry.is_directory();          // Go: entry.IsDir()

    // Get file size in bytes
    entry.file_size();             // Go: info.Size()

    // Get the full path
    entry.path();                  // Go: filepath.Join(dir, entry.Name())

    // Get just the filename
    entry.path().filename();       // Go: entry.Name()

    // Get the file extension
    entry.path().extension();      // Go: filepath.Ext(entry.Name())

    // Get filename without extension
    entry.path().stem();           // Go: strings.TrimSuffix(name, ext)
}
```

### Recursive directory listing

To go into subdirectories (like Go's `filepath.Walk`):

```cpp
for (const auto& entry : fs::recursive_directory_iterator("data")) {
    std::cout << entry.path() << std::endl;
}
```

### Useful `fs::path` operations

The `path` class is powerful — it handles OS-specific separators automatically:

```cpp
fs::path p("data/subfolder/file.txt");

p.filename();       // "file.txt"
p.stem();           // "file"
p.extension();      // ".txt"
p.parent_path();    // "data/subfolder"
p.root_path();      // "" (relative) or "C:\" (absolute on Windows)
p.is_absolute();    // false
p.is_relative();    // true
p.string();         // converts path to std::string
```

### Other filesystem operations

```cpp
// Check if file/directory exists
fs::exists("data/myfile.txt");              // Go: os.Stat() + os.IsNotExist()

// Create a directory
fs::create_directory("output");             // Go: os.Mkdir()

// Create nested directories
fs::create_directories("a/b/c/d");          // Go: os.MkdirAll()

// Remove a file
fs::remove("old_file.txt");                 // Go: os.Remove()

// Remove a directory and everything inside it
fs::remove_all("old_directory");            // Go: os.RemoveAll()

// Copy a file
fs::copy_file("src.txt", "dst.txt");        // Go: io.Copy() with manual open/create

// Rename / move
fs::rename("old.txt", "new.txt");           // Go: os.Rename()

// Get file size
fs::file_size("data.txt");                  // Go: info.Size()

// Get current working directory
fs::current_path();                         // Go: os.Getwd()
```

---

## 8. Putting It All Together — Real Patterns

### Pattern 1: List all files in `data/` and read each one

This is exactly what you need for your search engine:

```cpp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Collect all file paths from a directory
std::vector<std::string> ListFiles(const std::string& dirPath) {
    std::vector<std::string> files;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }

    return files;
}

// Read an entire file into a string
std::string ReadFile(const std::string& filePath) {
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::cerr << "Failed to open: " << filePath << std::endl;
        return "";
    }

    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    return content;
    // file closes automatically here — RAII
}

// Usage
int main() {
    std::vector<std::string> files = ListFiles("data");

    std::cout << "Found " << files.size() << " files" << std::endl;

    for (const auto& filePath : files) {
        std::string content = ReadFile(filePath);
        std::cout << "=== " << filePath << " ===" << std::endl;
        std::cout << content << std::endl;
    }

    return 0;
}
```

### Pattern 2: Read a file line by line and process each line

```cpp
void ProcessFile(const std::string& filePath) {
    std::ifstream file(filePath);

    if (!file.is_open()) {
        std::cerr << "Cannot open: " << filePath << std::endl;
        return;
    }

    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        lineNumber++;
        // Do something with each line
        std::cout << lineNumber << ": " << line << std::endl;
    }
}
```

### Pattern 3: Filter files by extension

```cpp
std::vector<std::string> ListFilesByExtension(
    const std::string& dirPath,
    const std::string& extension    // e.g. ".txt"
) {
    std::vector<std::string> files;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            files.push_back(entry.path().string());
        }
    }

    return files;
}

// Usage:
auto textFiles = ListFilesByExtension("data", ".txt");
```

### Pattern 4: Read all files and store filename→content in a map

```cpp
#include <map>

std::map<std::string, std::string> LoadAllFiles(const std::string& dirPath) {
    std::map<std::string, std::string> fileMap;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;

        std::string name = entry.path().filename().string();   // just the name
        std::string fullPath = entry.path().string();

        std::ifstream file(fullPath);
        if (!file.is_open()) continue;

        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        fileMap[name] = content;
    }

    return fileMap;
}

// Usage:
auto allFiles = LoadAllFiles("data");
std::cout << "Loaded " << allFiles.size() << " files" << std::endl;

// Access a specific file's content by name:
std::string content = allFiles["authentication-000028.txt"];
```

### Pattern 5: Write results to a new file

```cpp
void SaveResults(const std::string& outputPath,
                 const std::vector<std::string>& results) {

    // Create parent directories if they don't exist
    fs::create_directories(fs::path(outputPath).parent_path());

    std::ofstream file(outputPath);

    if (!file.is_open()) {
        std::cerr << "Cannot create: " << outputPath << std::endl;
        return;
    }

    for (const auto& result : results) {
        file << result << "\n";    // use \n not endl for performance
    }

    // file flushes remaining buffer and closes here (RAII)
}
```

---

## 9. Quick Reference Cheat Sheet

### Headers you need

```cpp
#include <fstream>      // ifstream, ofstream, fstream
#include <filesystem>   // directory_iterator, path, exists, etc.
#include <string>       // std::string
#include <iostream>     // cout, cerr (for debug output)
#include <vector>       // std::vector (to store file lists)
```

### The 6 operations you'll use 90% of the time

```cpp
namespace fs = std::filesystem;

// 1. LIST files in directory
for (const auto& entry : fs::directory_iterator("data")) { ... }

// 2. CHECK if it's a file
entry.is_regular_file()

// 3. GET path as string
entry.path().string()         // full path
entry.path().filename()       // just filename

// 4. OPEN for reading
std::ifstream file("path.txt");
if (!file.is_open()) { /* handle error */ }

// 5. READ entire file
std::string content((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

// 6. READ line by line
std::string line;
while (std::getline(file, line)) { /* process line */ }
```

### Go → C++ Quick Translation Table

```
Go                                  C++
─────────────────────────────────   ─────────────────────────────────────────
os.Open("f.txt")                    std::ifstream file("f.txt");
os.Create("f.txt")                  std::ofstream file("f.txt");
defer file.Close()                  // automatic (RAII)
io.ReadAll(file)                    std::string c((std::istreambuf_iterator<char>(f)),
                                                   std::istreambuf_iterator<char>());
bufio.NewScanner + scanner.Scan()   std::getline(file, line)
fmt.Fprintln(file, "text")         file << "text" << "\n";
os.ReadDir("dir")                   fs::directory_iterator("dir")
filepath.Walk("dir", fn)            fs::recursive_directory_iterator("dir")
os.Stat("f.txt")                    fs::exists("f.txt")
os.MkdirAll("a/b/c", 0755)         fs::create_directories("a/b/c")
os.Remove("f.txt")                  fs::remove("f.txt")
os.RemoveAll("dir")                 fs::remove_all("dir")
filepath.Ext("f.txt")               fs::path("f.txt").extension()
filepath.Base("a/b/f.txt")          fs::path("a/b/f.txt").filename()
filepath.Dir("a/b/f.txt")           fs::path("a/b/f.txt").parent_path()
```

---

## Common Gotchas Coming From Go

### 1. Relative paths are relative to the EXECUTABLE, not the source file
In Go, paths are relative to where you run `go run`. Same in C++, but the executable might be in `build/`:

```cpp
// If your executable is at build/searchengine.exe
// and data is at data/, you need:
std::ifstream file("../data/myfile.txt");   // go UP from build/

// OR run the executable from the project root:
// ./build/searchengine.exe    ← data/ is next to build/, so "data/myfile.txt" works
```

### 2. `std::endl` is not just a newline
It flushes the buffer. Use `"\n"` in loops for performance. Only use `std::endl` when you need to guarantee the output is written immediately.

### 3. The Most Vexing Parse
```cpp
// THIS IS A BUG — compiler thinks you're declaring a function!
std::string content(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());

// FIX: Extra parentheses around first argument
std::string content((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

// OR (C++11+): Use brace initialization — cleaner
std::string content{std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>()};
```

### 4. Windows path separators
`<filesystem>` handles this for you. Both work:
```cpp
fs::path("data/file.txt");      // forward slashes — works everywhere
fs::path("data\\file.txt");     // backslashes — Windows only
```

### 5. No built-in file permissions like Go's `os.FileMode`
C++ doesn't have a cross-platform equivalent of Go's `0644` permission bits in `os.OpenFile`. The `<filesystem>` library has `fs::permissions()` but it's not commonly used on Windows.

---

> **You now know everything you need to list, read, write, and manage files in C++.**  
> The key insight: streams + RAII + `<filesystem>` = no `defer`, no manual cleanup, no guessing file names.
