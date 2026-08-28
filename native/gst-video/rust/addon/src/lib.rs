//! Link shell for the gst_video N-API addon. All behavior lives in the C++
//! sources compiled by build.rs. The entry points below hand the module
//! registration to the C++ side (renamed there, since rustc trims cdylib
//! exports to Rust-declared symbols).

use std::ffi::c_void;

extern "C" {
    fn livi_gst_register_module_v1(env: *mut c_void, exports: *mut c_void) -> *mut c_void;
    fn livi_gst_module_api_version_v1() -> i32;
}

/// # Safety
/// Called by the Node runtime with a live napi_env/napi_value pair.
#[no_mangle]
pub unsafe extern "C" fn napi_register_module_v1(
    env: *mut c_void,
    exports: *mut c_void,
) -> *mut c_void {
    livi_gst_register_module_v1(env, exports)
}

#[no_mangle]
pub extern "C" fn node_api_module_get_api_version_v1() -> i32 {
    unsafe { livi_gst_module_api_version_v1() }
}
