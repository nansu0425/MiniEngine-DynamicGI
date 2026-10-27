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
* engine tuning, profiler, scene overlay, ray tracing, directional light, scene editor: ImGui View menu
* ray tracing debug view: pick one view in the View section of the Ray Tracing panel. Off shows the raster image
* ray query validation: pick a Validation view and what its Heatmap paints in the View section, then press Capture in the Validation section to read the last frame's counts and p50, p99, max. Fit to Capture sets a metric heatmap's range from the capture
* log and restore of the measurement conditions: Log in the Validation section prints the capture, and Log in the Path Tracer panel prints the path tracer's image, with the conditions they were drawn in (camera, resolution, TAA, directional light, Sky Light, path tracer settings and samples) as one line to the Output window. The path tracer's Log also copies the line. Paste such a line under Restore to set every condition in it, or nothing if the line lacks one or one cannot be set. The path tracer then starts over and stops at the samples of the line, so it draws the same image, after a restart too. A different resolution or mode is shown as a warning
* select: click an icon in the scene. Click empty space to deselect
* gizmo mode of the selected object: W translate, E rotate (without holding the right mouse button). The Scene Editor panel picks them too, and whether the rotate rings follow the object's own axes (Local) or the world axes (World). Translate always follows the world axes
* directional light: select its icon and rotate it with the rotate gizmo. It is a KHR_lights_punctual directional light, so the light goes along its local -Z, as the arrow shows, and turning it about its local Z (the blue ring with Local axes) leaves the light as it is. Its color and intensity (lux) are in the Directional Light panel, whose Reset Transform sets the transform to no rotation at the origin, so the light goes along world -Z

## Editor state:
On exit, ModelViewer saves its state in the working directory, or in the folder given by -settings_dir, and restores it on the next start.
* imgui.ini: panel layout, written by ImGui. Delete it to reset the layout
* editor.ini: window size and position, open panels, engine tuning values, editor camera, ray tracing view. Delete it to reset them

## Command line options for automation:
scripts/viewer.py starts ModelViewer with these options. They change nothing when omitted.
* -settings_dir <folder>: read and write editor.ini and imgui.ini in <folder> instead of the working directory
* -log <file>: also write the debug output to <file>, flushed at every message. While the D3D12 debug layer is on, its messages go there too, in the lines described in Core/Automation/D3D12MessageLog.h
* -gpu_debug 1: also turn on GPU-based validation of the D3D12 debug layer. The debug layer is always on in Debug, and in the other configurations only with -debug 1
* -unattended 1: for runs that nobody watches. What it changes is described in Core/Automation/UnattendedRun.h
* -control <name>: take commands on the named pipe \\.\pipe\<name> with the protocol described in Core/Automation/ControlChannel.h. Send `help` for the commands
* -hdr_output 0: present in SDR even on an HDR display

## glTF 2.0 Support:

Get sample assets from https://github.com/KhronosGroup/glTF-Sample-Models or make your own.

* Place asset folder underneath the ModelViewer folder
* Add to command line "-model [relative path to gltf or glb file]"
* Example:  ModelViewer.exe -model SciFiHelmet/glTF/SciFiHelmet.gltf

DirectXMesh and DirectXTex are used for compiling content the first time it is loaded.  For example, image files will have mip maps generated, encoded to a block compressed format, and then saved as a DDS for subsequent loading.  This means that first-time loading takes a little longer.
