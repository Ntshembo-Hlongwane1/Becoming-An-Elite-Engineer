# C++ Search Engine

A search engine built from first principles in C++.

## Prerequisites

To build and run this project, you need a C++ compiler (`g++`) and `CMake` installed. Since this setup is configured to run entirely from the **D Drive**, ensure the following directories exist:
* **Compiler**: `D:\mingw64\bin`
* **CMake**: `D:\cmake\bin`

---

## How to Build and Run

### 1. Using Git Bash (Recommended)

Open Git Bash and navigate to the project directory:
```bash
cd searchengine
```

#### Update PATH (if not already permanent)
If you haven't added the tools to your permanent path, run:
```bash
export PATH="/d/mingw64/bin:/d/cmake/bin:$PATH"
```

#### Generate build files (Only needed once or after adding new files)
```bash
cmake -S . -B build -G "MinGW Makefiles"
```

#### Build / Compile the project
```bash
cmake --build build
```

#### Run the executable
```bash
./build/searchengine.exe
```

---

### 2. Using PowerShell

Open PowerShell and navigate to the project directory:
```powershell
cd searchengine
```

#### Update PATH (if not already permanent)
```powershell
$env:Path = "D:\mingw64\bin;D:\cmake\bin;" + $env:Path
```

#### Generate build files (Only needed once or after adding new files)
```powershell
cmake -S . -B build -G "MinGW Makefiles"
```

#### Build / Compile the project
```powershell
cmake --build build
```

#### Run the executable
```powershell
.\build\searchengine.exe
```

---

## Project Structure

* `cmd/main.cpp` - The main entry point for the application.
* `CMakeLists.txt` - CMake configuration file. It will automatically detect any new `.cpp` files added under the `cmd/` or `src/` directories.
* `src/` - Place your library code here (headers `.h`/`.hpp` and source files `.cpp`).
* `data/` - Place your text documents or database files to search through here.
