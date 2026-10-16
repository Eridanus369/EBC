```

## Model

The server needs the DLSS NR model extracted from `nvngx_dlssnr.dll`:

```sh
./target/release/opendlss setup --dll /path/to/nvngx_dlssnr.dll
```

## Run

```sh
/tmp/opendlss-rs/target/release/opendlss-server
```

Blender connects on the first NR render. If the socket is absent the
renderer falls back to the native Film output.

## Wire protocol

Request header (40 B, little endian):
`magic(u32) width(u32) height(u32) seed(u32) auto_mask(u32)
 local_tone(f32) local_structure(f32) skin_structure(f32) style(f32)
 rgb_len(u32)`
followed by `rgb_len` bytes of RGB (row major).

Response header (20 B):
`magic(u32) status(u32) width(u32) height(u32) rgb_len(u32)`
followed by `rgb_len` bytes of RGB.
