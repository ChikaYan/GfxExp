# Graphics Experiments

適当にグラフィックス関連の論文などを読んで実装・検証したものを置きます。

このリポジトリを正しくcloneするには[Git LFS](https://git-lfs.github.com/)のインストールが必要です。

I'll randomly put something for implementing/validating graphics papers here.

You need to install [Git LFS](https://git-lfs.github.com/) to correctly clone this repository.

## 実装 / Implementations

### ReSTIR
Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting\
https://research.nvidia.com/publication/2020-07_Spatiotemporal-reservoir-resampling

![example](restir/comparison.png)

### ReGIR
Chapter 23. "Rendering Many Lights with Grid-based Reservoirs", Ray Tracing Gems II\
https://www.realtimerendering.com/raytracinggems/rtg2/index.html

![example](regir/comparison.png)

Models downloaded from Morgan McGuire's [Computer Graphics Archive](https://casual-effects.com/data)

## その他 / Miscellaneous
OptiX/CUDAのラッパーとして[OptiX Utility](https://github.com/shocker-0x15/OptiX_Utility)を使用しています。

Programs here use [OptiX Utility](https://github.com/shocker-0x15/OptiX_Utility) as OptiX/CUDA wrapper.

## 動作環境 / Confirmed Environment
現状以下の環境で動作を確認しています。\
I've confirmed that the program runs correctly in the following environment.

* Windows 10 (21H1) & Visual Studio Community 2019 (16.11.2)
* Core i9-9900K, 32GB, RTX 3080 10GB
* NVIDIA Driver 471.96

動作させるにあたっては以下のライブラリが必要です。\
It requires the following libraries.

* CUDA 11.3 Update 1 \
  OptiX Utilityは少し古いバージョンでも動作するとは思います。単にサンプルコードがこのバージョンに依存しているだけです。\
  ※CUDA 11.3.0にはバグがあり、OptiX Utilityと一緒に使用することができません。Update 1以降が必要です。\
  OptiX Utility may work with a bit older versions. The sample code just assumes this version.\
  \* CUDA 11.3.0 has a bug which prevents to use it with OptiX Utility. You need to use Update 1 or later.
* OptiX 7.3.0 (requires Maxwell or later generation NVIDIA GPU)

## オープンソースソフトウェア / Open Source Software
- [Open Asset Import Library (assimp)](https://github.com/assimp/assimp)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [gl3w](https://github.com/skaslev/gl3w)
- [GLFW](https://github.com/glfw/glfw)
- [stb](https://github.com/nothings/stb)
- [Tiny OpenEXR image library (tinyexr)](https://github.com/syoyo/tinyexr)
  - [miniz](https://github.com/richgel999/miniz)

----
2021 [@Shocker_0x15](https://twitter.com/Shocker_0x15)
