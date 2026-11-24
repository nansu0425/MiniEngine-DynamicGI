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
* engine tuning, profiler, scene overlay, scene hierarchy, details, Dynamic GI debug: ImGui View menu
* default of an engine tuning setting: right-click the setting and pick Reset to Default, or right-click a group and pick Reset to Defaults to reset every setting under it. Reset All at the top of Engine Tuning resets them all. The default is the value the setting has when user.ini does not have it
* scene: the Scene menu lists the scene files of the Scenes folder by name and marks the one open. Picking one opens it while running: the model changes, the objects in the Scene Hierarchy take their settings in the scene file, so the camera takes the scene file's pose and vertical field of view and the lights change. Changes to the objects not saved, such as a camera moved since, are lost. The settings, camera movement speed and UI stay. Save at the top of the menu writes the settings of the objects, the camera's pose and field of view among them, to the open scene file and leaves its other lines
* ray tracing debug view: pick one as View under Ray Tracing in Engine Tuning. Off shows the raster image
* select: click an object in the Scene Hierarchy panel, which lists the Model (the glTF model of the scene file), the Camera, the Directional Light, the Sky and, while the scene has them, the Dynamic GI Volume, the Point Light and the Spot Light, or click an icon in the scene. Click empty space in the scene to deselect. The Details panel shows the settings of the selected object
* gizmo mode of the selected object: W translate, E rotate, R scale (without holding the right mouse button). The toolbar at the top right of the scene picks them too, and whether the rotate rings follow the object's own axes (Local) or the world axes (World). Translate always follows the world axes. Scale resizes the object along its own axes, and an object that cannot be resized, such as a light, ignores it. Rotate does nothing to the point light
* camera: the Camera in the Scene Hierarchy is the camera you look through, so it has no icon or gizmo. Move it with the controls above, or set its position, heading, pitch and vertical field of view in its Details panel
* directional light: select its icon and rotate it with the rotate gizmo. It is a KHR_lights_punctual light whose type is Directional, so the light goes along its local -Z, as the arrow shows, and turning it about its local Z (the blue ring with Local axes) leaves the light as it is. Its Details panel shows the type, which the scene file keeps as Type, and has its color and intensity (lux) and Reset Transform, which sets the transform to no rotation at the origin, so the light goes along world -Z
* point light and spot light: KHR_lights_punctual lights of type Point and Spot, which nothing renders yet. A scene has at most one of each, and only while its scene file has a Point Light or Spot Light section. Add Point Light and Add Spot Light in the Scene Hierarchy panel add one at the origin, and Remove in its Details panel takes it out; Scene > Save writes or removes the section. Move them with the translate gizmo. The point light shines in every direction and does not rotate. The spot light shines along its local -Z, as the arrow shows, and rotates with the rotate gizmo. While the spot light is selected, its outer cone is drawn with its side lines and end circle, and its inner cone with a fainter circle, at a fixed length on screen. Limit Range in their Details panels gives a light a range, the distance beyond which it lights nothing, in metres of the glTF model while the position is in world units. Without it the light reaches infinitely far and the scene file has no Range. While a light with a range is selected, the point light's range is drawn as a sphere of three circles and the spot light's cones reach the range instead of the fixed length. Their Details panels have the color, the intensity (cd), the range and the position, and the spot light's also has the inner and outer cone angles in degrees, which the scene file keeps in radians, and Reset Transform
* Dynamic GI Volume: the box the probe grid of Dynamic GI covers, shown as an icon at its center. While it is selected, its box is drawn with translucent faces that the scene hides where it is in front of them, and with edges that stay visible, faint where the scene hides them. A scene has at most one, and only while its scene file has a Dynamic GI Volume section. Add Dynamic GI Volume in the Scene Hierarchy panel adds one over the model's bounds, and Remove in its Details panel takes it out; Scene > Save writes or removes the section. Move it with the translate gizmo and resize it along the world axes with the scale gizmo. It does not rotate. Its Details panel has its position, size, Fit to Model and the probe and weighting settings. The probe counts stay as it is resized, so the probe spacing follows the size, and moving or resizing it clears the probes. Without a volume the Probe source gives no indirect light
* Dynamic GI debug: the Dynamic GI Debug panel picks the indirect light source (Off, Path Traced, Probe) and the view, has the path traced reference's settings, pauses and steps the probe updates, and shows the probes and the probes a pixel interpolates

## User settings:
On exit, ModelViewer saves these settings in the working directory, or in the folder given by -settings_dir, and restores them on the next start.
* imgui.ini: panel layout, written by ImGui. Delete it to reset the layout
* user.ini: window size and position, open panels, engine tuning values (with the ray tracing view settings), Dynamic GI Debug panel settings, editor camera movement speed. Delete it to reset them. The camera's position, direction and field of view are not saved here, as they belong to the scene: Scene > Save writes them to the scene file

## Command line options for automation:
scripts/viewer.py starts ModelViewer with these options. They change nothing when omitted.
* -settings_dir <folder>: read and write user.ini and imgui.ini in <folder> instead of the working directory
* -log <file>: also write the debug output to <file>, flushed at every message. While the D3D12 debug layer is on, its messages go there too, in the lines described in Core/Automation/D3D12MessageLog.h
* -gpu_debug 1: also turn on GPU-based validation of the D3D12 debug layer. The debug layer is always on in Debug, and in the other configurations only with -debug 1
* -unattended 1: for runs that nobody watches. What it changes is described in Core/Automation/UnattendedRun.h
* -control <name>: take commands on the named pipe \\.\pipe\<name> with the protocol described in Core/Automation/ControlChannel.h. Send `help` for the commands
* -hdr_output 0: present in SDR even on an HDR display

## glTF 2.0 Support:

Get sample assets from https://github.com/KhronosGroup/glTF-Sample-Models or make your own.

* Place asset folder underneath the ModelViewer folder
* Write a scene file for it: the glTF path, the model's scale and the camera's starting pose, and the camera's vertical field of view and the lights if not the defaults. Scene > Save writes the camera and the lights as you set them. The keys are described above SceneFile in ModelViewer/ModelViewer.cpp, and Scenes/Sponza.ini is an example
* Add to command line "-scene [scene file path relative to the ModelViewer folder]". Without it ModelViewer opens Scenes/Sponza.ini
* Example:  ModelViewer.exe -scene Scenes/Sponza.ini
* Scenes/CornellBox.ini opens the Cornell box that scripts/fetch-assets.ps1 fetches and converts to glTF
* The camera always starts at the scene file's pose and vertical field of view. While running, the Scene menu opens another scene file of the Scenes folder (Controls above)

DirectXMesh and DirectXTex are used for compiling content the first time it is loaded.  For example, image files will have mip maps generated, encoded to a block compressed format, and then saved as a DDS for subsequent loading.  This means that first-time loading takes a little longer.
