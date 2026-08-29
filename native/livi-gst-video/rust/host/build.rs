// Compiles the gst_video sources with LIVI_GST_HOST_STANDALONE into the
// livi-gst-host binary (Linux only); its C++ main() is the process entry.
fn main() {
    println!("cargo:rerun-if-changed=../../src");
    if !cfg!(target_os = "linux") {
        return;
    }

    let mut includes = Vec::new();
    for pkg in ["gstreamer-1.0", "gstreamer-app-1.0", "gstreamer-video-1.0"] {
        let lib = pkg_config::Config::new().cargo_metadata(false).probe(pkg).unwrap();
        includes.extend(lib.include_paths);
    }

    let mut cpp = cc::Build::new();
    cpp.cpp(true)
        .std("c++17")
        // napi callbacks legitimately ignore their info parameter
        .flag_if_supported("-Wno-unused-parameter")
        .define("LIVI_GST_HOST_STANDALONE", None)
        .includes(&includes)
        .link_lib_modifier("+whole-archive")
        .file("../../src/gst_video.cc")
        .file("../../src/cp_screen_receiver.cc")
        .compile("gst_video_host_cpp");

    // The screen receiver's AEAD comes from the livi-crypto-node dependency.
    let mut c = cc::Build::new();
    c.includes(&includes)
        .file("../../src/cp_video_nal.c")
        .compile("gst_video_host_c");

    // Link flags go last: GNU ld resolves left to right, and shared libs
    // emitted before the archives are dropped under --as-needed.
    for pkg in ["gstreamer-1.0", "gstreamer-app-1.0", "gstreamer-video-1.0"] {
        pkg_config::Config::new().probe(pkg).unwrap();
    }
}
