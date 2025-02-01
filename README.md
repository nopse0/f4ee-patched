# Build Instructions

Create a folder, and in this folder checkout the 'f4se' and 'common' Git repositories from Ian Patt [(link)](https://github.com/ianpatt/f4se.git). On Windows you can use TortoiseGit and its Git Bash for this. In the f4se folder, you have to checkout the right f4se release tag, matching your Fallout 4 runtime version. For example, for Fallout 1.10.163 the f4se release tag is v0.6.23. This can be done in Git Bash with the command:
```
cd f4se
git checkout tags/v0.6.23
```

The 'common' module must be build and installed as described on Ian Patt's site (not needed for the 'f4se' module, only it's sources and CMakeFiles are used):
```
cmake -B common/build -S common -DCMAKE_INSTALL_PREFIX=extern common
cmake --build common/build --config Release --target install
```

Then checkout the Git repository from here, 'f4ee-patched'. In your folder, you should now have the sub directories
```
common
extern
f4se
f4ee-patched
```

f4ee-patched uses CMake, and must be configured first (to create the Visual Studio project files, etc.):
```
cmake -B f4ee-patched/build -S f4ee-patched --preset=ALL -DCMAKE_INSTALL_PREFIX=extern f4ee-patched
```

It then can be built either with the created Visual Studio project files or with CMake in the same way as Ian Patt's libraries are built by running (in the top level directory):
```
cmake --build f4ee-patched/build --config Release
```
