# Overview

TracerBoy is a GPU path tracer written in C++ and DX12. Largely this is a hobby project that I work on for fun. Over the years I've continued to iterated on TracerBoy so that it's capable of nice looking renders while also being lightweight and fast.  

![Example of TracerBoy rendering a van with UI on the right](https://wallisc.github.io/assets/TracerBoy/Van.jpg)

# Physically Based Path Tracing

Rendering is physically based using path tracing and supports a variety of materials/BRDFs.

![Render of the bistro scene](https://wallisc.github.io/assets/TracerBoy/bathroom.jpg)


<!--- Reference scenes have been validated against the reference renderer from [PBRT][PBRT] --->

# GPU-Accelerated

All path tracing is run on the GPU. Multiple optimizations have been implemented to both run fast and ensure good results can be gathered. Techniques include russian roulette, multiple importance sampling, and next-event estimation. While performance is both dpeendant on hardware and content, I find most PBRT content to run at >30 FPS on a RTX 30xx GPU.

<video controls loop autoplay muted>
    <source src="https://github.com/wallisc/wallisc.github.io/raw/master/assets/TracerBoy/Demo.mp4" type="video/mp4">
    Your browser does not support the video tag.
</video>

> Bistro scene from the [ORCA library][Orca]


# ML-based Denoising

TracerBoy uses a custom port of [Open Image Denoise][OIDN] written in [DirectML][DML] so that it can integrate with TracerBoy's DX12 pipeline. HW acceleration of ML will be used if available (i.e. Tensor Cores) but is not required.

![Comparison of raw path traced image vs a denoised image](https://wallisc.github.io/assets/ML/DenoisedKitchen.jpg)
> Raw path traced output at 8 samples per pixel on the left and same output but denoised on the right

# Compatible with all modern GPUs

TracerBoy will run on any DX12 GPU with support for Shader Model 6. TracerBoy is optimized for GPUs with support for [DirectX Raytracing][DXR] and will make full use of hardware support for raytracing. However, TracerBoy also has a software-raytracing implementation used for GPUs without hardwar raytracing which can allow it to run even on laptops

![Rendering on a Surface Pro](https://wallisc.github.io/assets/ML/surface.jpg)
> TracerBoy running on the integrated GPU of my Surface Pro 8

Fun fact: TracerBoy originated as a ShaderToy and the core of it's path tracing code is intentionally written so that it can be run via ShaderToy. In fact, the name TracerBoy is largey a mash up between the words ShaderToy and GameBoy. I periodically update this ShaderToy with the latest TracerBoy updates [here][TracerBoyShaderToy].

![TracerBoy in ShaderToy](https://wallisc.github.io/assets/TracerBoy/TracerBoyShaderToy.jpg)

# Easy to build

A big emphasis for TracerBoy has been avoiding unnecessary dependancies. The only requirement to build TracerBoy is installing [Visual Studio 2022][VS2022]. You should be able to pull down the source, open the solution, and hit run.

[VS2022]: https://visualstudio.microsoft.com/vs/
[PBRT]: https://pbrt.org/
[DML]: https://microsoft.github.io/DirectML/
[DXR]: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
[TracerBoyShaderToy]: https://www.shadertoy.com/view/fl3fRS
[Orca]: https://developer.nvidia.com/orca
[Benedikt]: https://benedikt-bitterli.me/resources/
[OIDN]: https://www.openimagedenoise.org/