Description
===========

waifu2x filter for VapourSynth, based on the w2xc library.


Note
====
The folder `models` must be located in the same folder as `Waifu2x-w2xc.dll`.

The filter will generate .bin files at the same location of the model files for the first time it runs. Make sure that your executable has write permission to the folder of the model files. The filter still can run even without the .bin files generated, but the performance will degrade. When you update the model files in the future, remember to delete the .bin files as well.


Usage
=====

    w2xc.Waifu2x(clip clip[, int noise=1, int scale=2, int block=512, bint photo=False, int gpu=1, bint log=False])

* clip: The input clip. Must be 32-bit floating point. Any planar color family is supported.

* noise: Noise reduction level.
  * 0 = none
  * 1 = low
  * 2 = high

* scale: Upscaling factor. Must be a power of 2. Set to 1 for no upscaling.

* block: The block size for dividing the image during processing. Smaller value results in lower VRAM usage, while larger value may give faster speed. The optimal value may vary according to different graphics card and image size.

* photo: When set to false, the anime model will be used for both denoising and upscaling. When set to true, the photo model will be used only for upscaling. Currently there is no photo model released for denoising yet, so the anime model will be used for denoising at the moment, which may give inferior results. There are two sets of models for anime, one is trained under RGB, the other is trained under Y (luma only). The RGB model will be used when the color family of the input is RGB, and the Y model will be used for all the other color family. Note that the photo model is only available for RGB, so this parameter has no effect for non-RGB input.

* gpu: Controls the environment to use.
  * 0 = disable GPU
  * 1 = auto detect. It will run on the first available environment in the following order:
    * CUDA
    * AMD OpenCL
    * FMA
    * AVX
    * Intel OpenCL
    * OpenCV filter2D
  * 2 = force to use OpenCL on Intel platform

* log: Whether the internal processing stats will be written to stdout.


Dependencies
============
[w2xc](https://github.com/tanakamura/waifu2x-converter-cpp)
