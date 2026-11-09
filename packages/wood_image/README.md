# wood_image 0.2.0

Image decoding, encoding, pixel operations, 2D drawing and 3D mesh rendering in pure wood. Listed in the
catalog as `wood@image`. It needs no native library and works on every supported
platform.

```sh
wood add wood@image
```

```wood
import image.image as img
import image.codecs as codecs

picture = codecs.load("photo.png")
codecs.save("thumbnail.png", img.resize(img.grayscale(picture), 160, 100))
```

## Modules

| Module | Purpose |
| --- | --- |
| `image` | RGBA8 `Image`, `crop`, bilinear `resize`, `grayscale`, `invert`, flips, `fill_rect`, `draw_line`, `fill_circle`, alpha `blit` |
| `codecs` | PNG read/write, BMP read/write, PPM write; `load` detects the format and `save` follows the extension |
| `scene` | Draws a `std.geometry` mesh with perspective, a depth buffer and one directional light: `render`, `framing`, `camera`, `style` |

## Formats

| Format | Read | Write |
| --- | --- | --- |
| PNG | 8-bit grayscale, gray+alpha, RGB, RGBA and palette, all filters | RGBA with stored (uncompressed) deflate |
| BMP | Uncompressed 24 and 32-bit, bottom-up | 24-bit |
| PPM | No | Binary P6 |

PNG chunks are checksum-verified and inflate output is bounded by the declared image
size, so a hostile file cannot cause unbounded allocation.

## Status and limits

Experimental. Interoperability was checked against Python's `zlib`: PNGs written here
decompress correctly, and a PNG with dynamic Huffman codes and all five filters decoded
with no mismatched bytes. Output PNGs are valid but large because they are not
compressed. Interlaced PNG, 16-bit PNG, JPEG, GIF and color management are not
supported. Processing is single-threaded and suited to images of moderate size.

Licensed MIT OR Apache-2.0.

## 3D meshes

`std.geometry` loads, builds and transforms meshes, and `scene` draws them to an image:

```wood
import image.codecs as codecs
import image.scene as scene
import std.geometry as geo

mesh = geo.torus(64, 32)
codecs.save("torus.png", scene.render(mesh, 640, 480, scene.framing(mesh), scene.style()))
```

It is a software renderer for previews, thumbnails and tools, not real-time graphics. Triangles that cross the camera plane are
left out rather than clipped, and there are no textures or smooth shading. See [the example](../../examples/mesh_render/src/main.wd).
