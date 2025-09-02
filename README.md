# Infinite Procedural Terrain with Clipmaps

This project is a procedural terrain generator inspired by the work of Mike J Savage and TheGodojo:
- [https://mikejsavage.co.uk/geometry-clipmaps/](https://mikejsavage.co.uk/geometry-clipmaps/)  
- [https://github.com/TheGodojo/Massive-Terrain-LOD-And-Stitching-COMPLETE](https://github.com/TheGodojo/Massive-Terrain-LOD-And-Stitching-COMPLETE)

Built in Godot, it leverages double-precision arithmetic to support virtually infinite terrain with high accuracy and stability.
NOTE: prior to indvidual repo, this project was created in a test repository. Prior commits and changes may not be viewable.

## Core Concepts

Terrain in the engine is generated using heightmaps, which are grayscale textures produced by a custom C++ noise generator. These heightmaps are fed into vertex shaders, which adjust the position of each vertex based on the height value stored in the red channel of the texture. This allows the surface of the terrain to update quickly and dynamically.

Of course, rendering the entire landscape at maximum detail would be overkill — especially since fine details far from the camera aren’t noticeable to the player. To optimize performance, the terrain is split into square chunks, each using a level-of-detail (LOD) system:  
<img src="showcase/lod_meshes.png" width="640"/>
- Chunks close to the camera use dense meshes with more vertices for high detail.
- Chunks further away use fewer vertices for lower detail.
- The chunk directly beneath the camera always uses the highest resolution.

A common problem with LOD terrain is the appearance of seams where two chunks of different resolutions meet:  
<img src="showcase/seam_crack.png" width="640"/>  
To fix this, edges are stitched together. The higher-resolution edge is interpolated so that every other vertex lines up with the lower-resolution edge:  
<img src="showcase/seam_stitched.png" width="640"/>  
This creates smooth transitions between chunks without resorting to heavier techniques like “skirts” or complex triangle stitching, making it both simple and efficient.

Geometry alone isn’t enough, though — lighting can break if normals are only calculated from the mesh resolution. To solve this, all heightmaps are generated at full quality regardless of the mesh’s vertex density. The fragment shader then interpolates lighting values as if every chunk had the same number of vertices. This has two major benefits:
- Normals look smooth across all chunks, no matter the LOD.
- Heightmaps can be reused freely across meshes of different resolutions.

Generating every heightmap at full quality adds some cost, but it solves multiple rendering problems at once and keeps the terrain looking consistent.  
The system also adapts as the camera moves. When the camera crosses certain thresholds, chunks are shifted around dynamically to keep coverage continuous without rendering unnecessary detail:
<img src="showcase/wireframe_demo.gif" width="320"/>
<img src="showcase/normal_demo.gif" width="320"/>

Behind the scenes, all heightmaps are stored in a HashMap, with their keys representing world-space coordinates. If a heightmap for a given location doesn’t exist yet, it gets generated on the fly. Otherwise, the existing one is reused. Since shaders only need references to these textures (not copies), assigning them to chunks is fast and lightweight.



## Design Alternatives

GPU noise generation was considered, but the CPU would need to get heightmaps for collision meshes. The sync times and VRAM strage size were too costly. Double precision based noise generation would also not be possible. 

GPU Mesh Tessellation was considered, as it would increase consistency, and smoother LOD transitions. However, it appears that modern renderers do not implement tessellation in the same way older apis did. Godot does not support tessellations, and the gains compared to the effort required to implement the shader pipeline would not be justified.
