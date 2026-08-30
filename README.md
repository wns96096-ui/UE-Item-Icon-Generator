# Item Icon Generator for Unreal Engine

![Selecting static meshes and adding them to the icon-generation workflow](Docs/Media/select-static-meshes.gif)
![Transparent Texture2D icon generated from a static mesh](Docs/Media/generated-transparent-icon.gif)

An editor-only Unreal Engine plugin for turning static meshes into `Texture2D` item icons. It is intended for teams that need a consistent icon-production workflow without repeatedly setting up capture actors, lights, and output assets by hand.

## What it solves

Creating UI icons from meshes can become repetitive: select a mesh, set up a camera and lights, capture it, save an asset, and repeat. Item Icon Generator keeps those steps in one editor panel and lets you queue multiple static meshes for sequential generation.

## Features

- Batch icon generation for selected static meshes.
- Automatic preview generation and a session-only preview cache.
- Preview controls: drag to rotate the mesh, `W`/`A`/`S`/`D` to move the composition, and the mouse wheel to zoom.
- Three-point lighting with key, fill, and back lights, plus sky, exposure, saturation, light-color, and background controls.
- Saving captured pixels as `Texture2D` assets, including transparent backgrounds.
- Reuse of the preview scene, mesh component, and render targets while the panel is open; saved previews can use their cached pixels instead of requiring another capture.

## Material preview in a new project

In a new project, a mesh can initially appear gray when its materials or shaders are still compiling. Capturing during that state can record a fallback material. The plugin waits for pending asset and shader compilation before capture; if the first preview is still incomplete, wait for compilation to finish and use **Refresh**. Also check the mesh's material slots and any material compile errors.

## Install

### From a release package

1. Close Unreal Editor.
2. Download the UE 5.8 package from the release page and extract it.
3. Copy the `ItemIconGenerator` folder to `<YourProject>/Plugins/`.
4. Open the project and allow the editor to load or enable the plugin if prompted.
5. Open **Tools > Item Icon Generator**.

### From source

1. Clone this repository into `<YourProject>/Plugins/ItemIconGenerator`.
2. Regenerate project files if your IDE workflow requires it.
3. Build the editor target, then open the project in Unreal Editor.

## Use

1. Select one or more static meshes in the Content Browser.
2. Open **Tools > Item Icon Generator** and choose **Add Content Browser Selection**.
3. Set the output folder, naming rule, resolution, and transparent-background option.
4. Select a queue item and adjust its preview if needed.
5. Choose **Save Selected Preview** for one asset or **Generate All** for the enabled queue items.

The default output folder is `/Game/Generated/ItemIcons`, and the default naming rule is `T_Icon_{MeshName}`.

## Project layout

```text
Config/                 Plugin configuration
Content/                Plugin content (currently empty)
Docs/                   Documentation and future screenshots
Resources/              Plugin resources
Source/                 Editor module source
ItemIconGenerator.uplugin
README.md               English overview
README_KO.md            Korean detailed guide
QUICK_START_KO.md       Korean quick start for teammates
CHANGELOG.md
LICENSE
```

## Engine support

This release targets Unreal Engine 5.8 on Windows. The module is editor-only and is not included in packaged game builds.

## Build

Use Unreal Engine's `RunUAT BuildPlugin` command against `ItemIconGenerator.uplugin`, with a package directory outside the repository. Release packages should exclude `Intermediate` and PDB files.

## Documentation

- [Korean detailed guide](README_KO.md)
- [Korean team quick start](QUICK_START_KO.md)

## License

Released under the [MIT License](LICENSE).

## Screenshots to add

- Tools menu entry
- Main panel and queue
- Output settings
- Preview controls
- Lighting settings and before/after comparison
- Batch progress
- Generated `Texture2D` assets in the Content Browser
