//! Persistent NR server: loads the model once, then serves frames over a Unix socket.
//! Protocol:
//!   request  header (40 B): magic, width, height, seed, auto_mask, local_tone,
//!                           local_structure, skin_structure, style, rgb_len
//!            payload: rgb_len bytes (RGB, row-major)
//!   response header (20 B): magic, status, width, height, rgb_len
//!            payload: rgb_len bytes

use std::collections::HashMap;
use std::io::{ErrorKind, Read, Write};
use std::os::unix::net::{UnixListener, UnixStream};

use anyhow::{bail, Result};
use opendlss_nr::network::Network;
use opendlss_nr::runtime::Gpu;
use opendlss_nr::{Conditioning, Image, Model};

const SOCK_PATH: &str = "/tmp/opendlss-nr.sock";
const MAGIC: u32 = 0x4F4E_4C52;

fn main() -> Result<()> {
    let model_dir = opendlss_nr::config::model_dir()
        .ok_or_else(|| anyhow::anyhow!("no opendlss config dir"))?;
    let model = Model::load(model_dir, false)?;
    let gpu = pollster::block_on(Gpu::request())?;
    eprintln!("[SRV] {} via {}", gpu.adapter_name, gpu.backend);

    let _ = std::fs::remove_file(SOCK_PATH);
    let listener = UnixListener::bind(SOCK_PATH)?;
    eprintln!("[SRV] listening on {}", SOCK_PATH);

    let mut cache: HashMap<(u32, u32), Network> = HashMap::new();

    for stream in listener.incoming() {
        match stream {
            Ok(mut s) => {
                if let Err(e) = handle(&mut s, &model, &gpu, &mut cache) {
                    eprintln!("[SRV] {:#}", e);
                }
            }
            Err(e) => eprintln!("[SRV] accept: {}", e),
        }
    }
    Ok(())
}

fn handle(
    stream: &mut UnixStream,
    model: &Model,
    gpu: &Gpu,
    cache: &mut HashMap<(u32, u32), Network>,
) -> Result<()> {
    loop {
        let mut hdr = [0u8; 40];
        match stream.read_exact(&mut hdr) {
            Ok(()) => {}
            Err(e) if e.kind() == ErrorKind::UnexpectedEof => return Ok(()),
            Err(e) => return Err(e.into()),
        }
        let magic = u32::from_le_bytes(hdr[0..4].try_into().unwrap());
        if magic != MAGIC {
            bail!("bad magic 0x{:08x}", magic);
        }
        let width = u32::from_le_bytes(hdr[4..8].try_into().unwrap());
        let height = u32::from_le_bytes(hdr[8..12].try_into().unwrap());
        let seed = u32::from_le_bytes(hdr[12..16].try_into().unwrap());
        let auto_mask = u32::from_le_bytes(hdr[16..20].try_into().unwrap()) != 0;
        let local_tone = f32::from_le_bytes(hdr[20..24].try_into().unwrap());
        let local_structure = f32::from_le_bytes(hdr[24..28].try_into().unwrap());
        let skin_structure = f32::from_le_bytes(hdr[28..32].try_into().unwrap());
        let style = f32::from_le_bytes(hdr[32..36].try_into().unwrap());
        let rgb_len = u32::from_le_bytes(hdr[36..40].try_into().unwrap()) as usize;

        let mut rgb = vec![0u8; rgb_len];
        stream.read_exact(&mut rgb)?;

        let cond = Conditioning {
            seed,
            auto_mask,
            local_tone,
            local_structure,
            skin_structure,
            style,
        };

        let img = Image { width, height, rgb };

        let key = (width, height);
        if !cache.contains_key(&key) {
            eprintln!("[SRV] building {}x{} network", width, height);
            let t0 = std::time::Instant::now();
            let net = Network::new(gpu, model, width, height, cond, false)?;
            eprintln!("[SRV] build {:.1}s", t0.elapsed().as_secs_f64());
            cache.insert(key, net);
        }
        let net = cache.get(&key).unwrap();
        let (out, timings) = net.process(gpu, &img)?;

        let mut rhdr = [0u8; 20];
        rhdr[0..4].copy_from_slice(&MAGIC.to_le_bytes());
        rhdr[4..8].copy_from_slice(&0u32.to_le_bytes());
        rhdr[8..12].copy_from_slice(&out.width.to_le_bytes());
        rhdr[12..16].copy_from_slice(&out.height.to_le_bytes());
        rhdr[16..20].copy_from_slice(&(out.rgb.len() as u32).to_le_bytes());
        stream.write_all(&rhdr)?;
        stream.write_all(&out.rgb)?;
        stream.flush()?;

        eprintln!("[SRV] frame {:.1} ms", timings.frame_seconds * 1000.0);
    }
}
