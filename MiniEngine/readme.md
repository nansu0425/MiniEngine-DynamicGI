# MiniEngine by Team Minigraph at Microsoft

## Getting started:
* Open MiniEngine.slnx
* Select configuration: Debug (full validation), Profile (instrumented), Release
* Build and run

## Controls:
* look around: mouse while holding the right mouse button
* forward/backward/strafe: WASD while holding the right mouse button
* up/down in world space: E/Q while holding the right mouse button
* movement speed: mouse wheel while holding the right mouse button
* pan: drag with the middle mouse button
* toggle ImGui: F1
* engine tuning, profiler, scene overlay, ray tracing: ImGui View menu
* ray tracing debug view: pick one view in the View section of the Ray Tracing panel. Off shows the raster image
* select: click an icon in the scene. Click empty space to deselect
* gizmo mode of the selected object: W translate, E rotate (without holding the right mouse button)
* sun direction: select the sun icon and rotate it (green ring: azimuth, red ring: elevation). The arrow shows where the light goes

## glTF 2.0 Support:

Get sample assets from https://github.com/KhronosGroup/glTF-Sample-Models or make your own.

* Place asset folder underneath the ModelViewer folder
* Add to command line "-model [relative path to gltf or glb file]"
* Example:  ModelViewer.exe -model SciFiHelmet/glTF/SciFiHelmet.gltf

Notes:  Some IBL cube maps are provided for physically based rendering in the ModelViewer/Textures folder.  ModelViewer automatically detects cube maps in this folder so you can add your own.  They must follow the same naming convention for diffuse and specular maps.  You can change the active environment map in the tweak menu under the "ModelViewer" category.

DirectXMesh and DirectXTex are used for compiling content the first time it is loaded.  For example, image files will have mip maps generated, encoded to a block compressed format, and then saved as a DDS for subsequent loading.  This means that first-time loading takes a little longer.
