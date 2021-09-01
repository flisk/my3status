extern crate libc;

use libc::{c_char, c_void};

mod ffi {
    use libc::{c_char, c_void};
    use super::StatePtr;

    #[repr(C)]
    pub struct ModulePtr {
        state: StatePtr,
        name: *const c_char,
        output: *const c_char,
        pub output_visible: bool,
        output_mutex: c_void
    }

    #[link(name = "my3status")]
    extern {
        pub fn my3status_register_module(state: StatePtr, name: *const c_char,
                                         output: *const c_char, visible: bool)
            -> *mut ModulePtr;
        pub fn my3status_output_begin(m: *mut ModulePtr);
        pub fn my3status_output_done(m: *mut ModulePtr);
    }

}

pub type StatePtr = *const c_void;

pub struct State { ptr: StatePtr }

unsafe impl Send for Module {}

impl State {
    pub fn new(ptr: *const libc::c_void) -> Self {
        Self { ptr }
    }
}

pub struct Module { ptr: *mut ffi::ModulePtr }

impl Module {
    pub fn with_output_lock<F: FnOnce()>(&self, func: F) {
        unsafe { ffi::my3status_output_begin(self.ptr) }
        func();
        unsafe { ffi::my3status_output_done(self.ptr) }
    }

    pub fn visible(&self, v: bool) {
        unsafe { (*self.ptr).output_visible = v }
    }
}

pub fn register_module(s: State, name: &str, output: &str, visible: bool) -> Module
{
    unsafe {
        Module { ptr: ffi::my3status_register_module(
            s.ptr,
            name.as_ptr() as *const c_char,
            output.as_ptr() as *const c_char,
            visible
        )}
    }
}