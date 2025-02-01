# Description

This fixes a nasty bug in the LooksMenu 'f4ee.dll' (I posted this description also on the NexusMods LooksMenu site):

There is a nasty bug in the OverlayInterface::RemoveOverlay function. The UID of the removed overlay is not added to the list "m_freeIndices". This has the effect that if the list of free indices is empty and you add two overlays to an actor, and then remove the first one, that after that, all overlays you add will get the UID of the second one and overwrite it. It' s impossible after that to increase the number of overlays a npc has. This bug makes it practically impossible to use overlays with a limited duration with AAF (or NAF+NAFBridge). If there are other known bugs in the LooksMenu overlay f4ee.dll, feel free to send merge requests.

Example:
```
Int uid1 = Overlays.AddOverlay( <params for overlay 1> )
Int uid2 = Overlays.AddOverlay( <params for overlay 2> )
Overlays.RemoveOverlay(uid1)
```
After this, all overlays you add will have the value of uid2 and overwrite the second:
```
Int uid3 = Overlays.AddOverlay( <params for overlay 3> )
Int uid4 = Overlays.AddOverlay( <params for overlay 4> )
...
Int uidn = Overlays.AddOverlay( <params for overlay n> )
```
=> uid3 = uid4 = ... = uidn   = uid2

This is because the GetNextUID() function looks like this:

```
OverlayInterface::UniqueID OverlayInterface::GetNextUID()
{
    SimpleLocker locker(&m_overlayLock);

    OverlayInterface::UniqueID nextUID = 0;
    if(!m_freeIndices.empty()) {
        nextUID = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        nextUID = m_dataMap.size() + 1; // This only happens when free indices is empty, meaning we've filled gaps in m_dataMap
    }

    return nextUID;
}
```

So in RemoveOverlay() the removed UID must be added to the list of free indices (otherwise you get the error described above) !!!! 

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
