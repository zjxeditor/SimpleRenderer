# SimpleRenderer
### Summary

This is simple but efficient renderer with DirectX 12. It supports the following key features:

1. Forward rendering pass.
2. PBR material based on Cook Torrance model.
3. MSAA, Shadow Map, SSAO.
4. Instancing.
5. GPU buffer read back and screenshot.
6. Continuous and discrete rendering model.
7. FBX importer with FBX SDK.
8. Mesh topology calculation.
9. Mesh subdivision with OpenSubdiv.
10. Custom math library with geometry calculation.
11. Easy to use rendering interface.

What you can use it for:

1. View FBX mesh model through a custom and configurable way. Also support skeleton structure visualization.
2. Do mesh subdivision and view the result. Calculate the final points on the limit surface which you do subdivision for infinite times.
3. Integrate into your own 3D projects, so you don't need to care about the rendering part.
4. Learn DirectX 12 with this simple renderer.
5. Reuse the code such as geometry math calculation and FBX importation.

![1](.\images\1.png)   

![2](.\images\2.png) 

### Build

Requirements:

* Windows 10 OS
* Visual Studio 2017

There are three dependent libraries: `FBX SDK`, `Glog` and `OpenSubdiv`. All of the them are located in the `thirdparty` folder and their paths are already configured in the Visual Studio project. So just open it and compile.

### Structure

1. data: stores several FBX files for demo testing.
2. log: logging files go here.
3. mesh: mesh calculation related code.
4. rendering: main rendering logic with DirectX 12.Pay attention to  `deviceresources` and `renderresources` classes, they provide top level API access.
5. shaders: all HLSL shaders go here. Shaders will be compiled at initialization time during runtime for simple, so they won't participate in the Visual Studio building process. `lightingutil.hlsl` contains main logic for light calculation with `Cook Torrance` model and `Blinn Phong` model.
6. thirdparty: contains third-party libraries.
7. utility: provide utilities for math calculation.

### Discrete Model

We all familiar with continuous rendering model, which is that we do rendering work frame by frame. In this model, the project use frame resourced based CPU-GPU synchronization method to avoid idles.

However, sometimes we actually need a discrete rendering model. The process is simple: do rendering work -> read back rendering result from GPU -> do additional processing logic. So you can control the rendering iteration, what data to read back and what additional logic to do. This is very useful for some GPU based optimization algorithms.

### Customization

The idea of the this renderer is pretty simple: give it data and let it render. You can easily use it to do any rendering work. Pay attention to the `App` class under `rendering` folder, it is the main interface. You just need to derive your own `app` class from it and then override several key virtual methods.

1. Update: provide your own update logic such as changing material or moving lights.
2. OnMouseDown: provide your own logic for mouse down event. 
3. OnMouseUp: provide your own logic for mouse up event.
4. OnMouseMove: provide your own logic for mouse move event.
5. PreInitialize: provide your own logic for pre-initialization such as configure MSAA, discrete rendering mode, initial camera parameters and screen output size. Note, at this time, device resource and rendering resource are not created. You cannot access `DeviceResources` and `RenderResources` which are top level API interfaces.
6. PostInitialize: provide your own logic for post-initialization, for instance, FBX mesh file importation. Note, at this time device resource and rendering resource are already created, your can access `DeviceResources` and `RenderResources` safely.
7. AddRenderData: provide your logic to add the actual data needed for rendering. In general, your should create materials and 3D mesh data. Then create different render items and use `RenderResources` interface to add them.
8. DiscreteEntrance: if you configured the discrete model in the `PreInitialize`  method, then all the code in this method will be executed. The application will be exited after this method.

### Demo

There are 5 demos for you to get familiar with this simple renderer. 

* demo0: demonstrate the basic usage of the simple render system. It will render a simple scene and you can navigate in the scene.
* demo1: demonstrate the usage of FBX importer. It will import a FBX file and render it for you.

* demo2: demonstrate the usage of mesh subdivision and limit surface point evaluation. It uses instancing for limit surface points rendering.

* demo3: demonstrate the discrete render mode, which only render the scene once and save the result to a local image. In this demo, we encode the 24bit depth data into 3 channels of the rgb image.

* demo4: demonstrate the material model in the render system. You can play with a material sphere.

### Notes

1. Camera navigation keys are `WASD`. Press and hold the left mouse button, then move the mouse to change the camera view direction.
2. `VSync` is enabled by default. If you want to disable `VSync`, in `DeviceResources::Present` method change the code `HRESULT hr = mSwapChain->Present(1, 0);` to `HRESULT hr = mSwapChain->Present(0, 0);`.
3. Texture is not supported yet.